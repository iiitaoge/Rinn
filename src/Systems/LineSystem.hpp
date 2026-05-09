#pragma once
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"
#include "EventSystem.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// =====================================================================
// LineSystem - M6 F5 (v2)
// ---------------------------------------------------------------------
// "对话是动作的外显" - 不再 wildcard 监听所有事件,
// 而是被 DecisionSystem (动作选定时) 和 AppraisalSystem (witness 反应时)
// 直接调用 → 1:1 同步
//
// 选择算法: candidates 按 utility-style score (gain × salience × emotion_mod)
// 采样: softmax (跟 DecisionSystem 同形)
// 输出: TextBubble (复用既有 Component, raylib 中文字体渲染)
// =====================================================================

namespace Rinn::LineSystem {

    struct LineDef {
        const char* template_str;                                 // {actor} {target} 占位
        std::array<float, EmotionComponent::E> emotion_modulators;
        std::array<float, NeedComponent::N>    need_relevance;
        EventBus::EventType  trigger_event;                       // None = 通用候选
        float base_weight;
    };

    inline Registry* g_reg = nullptr;
    inline float TEMPERATURE = 1.0f;
    inline float COOLDOWN_SEC = 4.0f;
    inline std::unordered_map<uint32_t, float> last_speak_time;
    inline float current_time_acc = 0.0f;

    // 名字 getter (LineSystem 不直接 include AIDebugUI; 由 main.cpp 注入)
    using NameGetter = std::string(*)(Entity);
    inline NameGetter g_name_getter = nullptr;

    // 35 行 catalog
    inline std::vector<LineDef> line_catalog = {
        // ═══ 加税触发 (TaxIncreased) ═══
        { "我又被加税了！这是要逼死人吗？",         {1.5f,0.5f,0,0,0},     {1.5f,0,0,0,0,0},   EventBus::EventType::TaxIncreased, 1.0f },
        { "得藏点东西…不然撑不过这关",              {0,2.0f,0.5f,0,0},     {1.0f,0,0,1.0f,0,0},EventBus::EventType::TaxIncreased, 0.9f },
        { "天理何在！村长这是吸我们的血！",          {2.0f,0.5f,0,0.5f,0},  {1.0f,0,0,0,1.5f,0},EventBus::EventType::TaxIncreased, 0.8f },
        { "唉，又来。习惯了。",                     {0,0.3f,0,0.5f,0.3f},  {0.5f,0,0,0,0,0},   EventBus::EventType::TaxIncreased, 0.5f },
        { "保佑村里平安渡过此劫…",                  {0,0.5f,0,0.5f,0.5f},  {0,0,0,0,2.0f,0},   EventBus::EventType::TaxIncreased, 0.7f },
        { "财政现实如此，加税在所难免。",           {0,0.5f,0,0,0},        {0,0,0,2.0f,0,0},   EventBus::EventType::TaxIncreased, 0.6f },

        // ═══ 神父之死 (PriestDied) ═══
        { "神父走了…我们的精神支柱倒了",            {0,0,0.5f,1.5f,1.0f},  {0,0,0,0,2.0f,0},   EventBus::EventType::PriestDied, 1.0f },
        { "神父…他是村里最后的好人啊",              {0,0,0,2.0f,0.5f},     {0,0,1.0f,0,1.0f,0},EventBus::EventType::PriestDied, 0.9f },
        { "那个老头终于走了。可惜没人替他了。",     {0,0,0,1.0f,0.5f},     {0,1.0f,0,0,0.5f,0},EventBus::EventType::PriestDied, 0.4f },

        // ═══ 砸石碑 (BrokeDown) ═══
        { "{actor}疯了吗？砸了石碑！",              {0.5f,0,1.5f,0,0},     {0,0,0,1.0f,0,0},   EventBus::EventType::BrokeDown, 1.0f },
        { "这种人留在村里就是隐患…",                 {0.8f,0.5f,0,0,0},     {0,0,0,1.5f,0,0},   EventBus::EventType::BrokeDown, 0.7f },
        { "砸了它！这石碑就是吸血鬼！",              {3.0f,0,0,0,0},        {1.5f,0,0,0,0,0},   EventBus::EventType::BrokeDown, 1.2f },  // actor
        { "天哪…这下要出大事了",                    {0,1.5f,2.0f,0.5f,0},  {0,0,0,1.0f,1.0f,0},EventBus::EventType::BrokeDown, 0.9f },

        // ═══ 拒税 (TaxRefused) ═══
        { "{actor}居然敢拒税！要变天了？",          {0.5f,1.0f,1.5f,0,0},  {1.0f,0,0,1.0f,0,0},EventBus::EventType::TaxRefused, 1.0f },
        { "我也想拒税，但是…我不敢",                 {0.5f,1.5f,2.0f,0.5f,0},{0,0,0,2.0f,0,0},  EventBus::EventType::TaxRefused, 0.6f },
        { "他做对了！村长该尝尝苦头！",              {2.5f,0,0,0,0},        {1.0f,1.0f,0,0,0,0},EventBus::EventType::TaxRefused, 0.7f },
        { "我宁死也不交这血汗钱！",                  {2.5f,0,0,0,0},        {1.5f,0,0,0,0,0},   EventBus::EventType::TaxRefused, 1.1f },  // actor

        // ═══ 找村长理论 (ConfrontedLeader) ═══
        { "{actor}去找村长理论了，不知道结果会怎样", {0,1.5f,0,0,0},        {0,1.5f,0,0,0,0},   EventBus::EventType::ConfrontedLeader, 0.9f },
        { "村长，你这样做对得起村里人吗！",          {2.0f,0,0,0,0},        {0,2.0f,0,0,0,0},   EventBus::EventType::ConfrontedLeader, 1.0f }, // actor

        // ═══ 囤粮 (Hoarded) ═══
        { "{actor}也开始囤了…我也得囤点什么",        {0,2.0f,0.5f,0,0},     {1.0f,0,0,1.0f,0,0},EventBus::EventType::Hoarded, 0.8f },
        { "得多藏点粮食…",                          {0,2.5f,0,0,0},        {1.0f,0,0,1.0f,0,0},EventBus::EventType::Hoarded, 1.0f },  // actor

        // ═══ 私密恐慌动作 ═══
        { "把门栓上…谁也别进来",                    {0,0.5f,2.5f,0,0},     {0,0,0,1.5f,0,0},   EventBus::EventType::ClosedDoors, 1.0f },
        { "我…我病了，不能见人",                    {0,0,2.0f,0.5f,0},     {0,0,0,1.0f,0,0},   EventBus::EventType::FeignedIllness, 1.0f },
        { "村长大人英明，我等愚民心服口服…",         {0,0,2.5f,0,0.5f},     {0,0,0,1.0f,0,0},   EventBus::EventType::Flattered, 1.0f },
        { "钱不是命，但藏起来才是命。",              {0,2.5f,0,0,0},        {1.5f,0,0,0,0,0},   EventBus::EventType::HidMoney, 1.0f },
        { "先把矿工预付银子付了，免得手头没人用",    {0,2.0f,0,0,0},        {1.5f,0,0,1.0f,0,0},EventBus::EventType::PrepaidMiners, 1.0f },

        // ═══ 接触型 (孤独动作) ═══
        { "老兄，今天能聊聊吗？",                   {0,0,0,0.5f,2.0f},     {0,1.5f,0,0,0,0},   EventBus::EventType::VisitedFriend, 1.0f },
        { "去酒馆喝一杯，谁都别拦我",                {0,0,0,0.5f,2.5f},     {0,1.5f,0,0,0,0},   EventBus::EventType::WentToTavern, 1.0f },
        { "我们一起送他最后一程吧…",                 {0,0,0,1.5f,2.0f},     {0,1.0f,1.0f,0,0,0},EventBus::EventType::AttendedFuneral, 1.0f },

        // ═══ 悲伤系 ═══
        { "我心里堵得慌…谁能听我说说",               {0,0,0,2.0f,0.5f},     {0,0,2.0f,0,0,0},   EventBus::EventType::ConfidedGrief, 1.0f },
        { "我的孩子…我的孩子啊…",                   {0,0,0,2.5f,1.0f},     {0,0,2.5f,0,0,0},   EventBus::EventType::SankIntoGrief, 1.0f },
        { "愿苍天保佑我们这卑微的村庄",              {0,0,0,0.5f,0.5f},     {0,0,0,0,2.5f,0},   EventBus::EventType::Prayed, 1.0f },

        // ═══ 通用低强度喃喃 (trigger = None) ═══
        { "唉…",                                    {0,0,0,1.5f,0.5f},     {0,0,1.0f,0,0,0},   EventBus::EventType::None, 0.3f },
        { "这日子怎么过…",                          {0,1.5f,0.5f,0.5f,0},  {1.0f,0,0,1.0f,0,0},EventBus::EventType::None, 0.3f },
    };

    // ─── 算分 (跟 DecisionSystem::compute_utility 同构) ───
    inline float compute_score(const NeedComponent& need,
                               const EmotionComponent& emo,
                               const LineDef& line) {
        float emo_score = 1.0f;
        for (int i = 0; i < EmotionComponent::E; ++i) {
            emo_score += emo.intensity[i] * line.emotion_modulators[i];
        }
        float need_score = 0.0f;
        for (int i = 0; i < NeedComponent::N; ++i) {
            float gap = std::max(0.0f, need.expectation[i] - need.satisfaction[i]);
            float salience = need.weights[i] * gap;
            need_score += salience * line.need_relevance[i];
        }
        return line.base_weight * emo_score * std::max(0.5f, need_score);
    }

    inline std::optional<size_t> select_line(const NeedComponent& need,
                                             const EmotionComponent& emo,
                                             EventBus::EventType trigger) {
        std::vector<std::pair<float, size_t>> candidates;
        for (size_t i = 0; i < line_catalog.size(); ++i) {
            const auto& line = line_catalog[i];
            if (line.trigger_event != EventBus::EventType::None &&
                line.trigger_event != trigger) continue;
            float s = compute_score(need, emo, line);
            if (s > 0.5f) candidates.emplace_back(s, i);
        }
        if (candidates.empty()) return std::nullopt;

        float max_s = -1e9f;
        for (auto& [s, _] : candidates) max_s = std::max(max_s, s);
        float sum = 0.0f;
        std::vector<float> probs(candidates.size());
        for (size_t i = 0; i < candidates.size(); ++i) {
            probs[i] = std::exp((candidates[i].first - max_s) / TEMPERATURE);
            sum += probs[i];
        }

        static std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist(0.0f, sum);
        float r = dist(rng);
        float cum = 0.0f;
        for (size_t i = 0; i < candidates.size(); ++i) {
            cum += probs[i];
            if (r <= cum) return candidates[i].second;
        }
        return candidates.back().second;
    }

    inline std::string instantiate(const char* tmpl,
                                   const std::string& actor_name,
                                   const std::string& target_name) {
        std::string s = tmpl;
        auto replace = [&](const std::string& key, const std::string& val) {
            size_t pos;
            while ((pos = s.find(key)) != std::string::npos) {
                s.replace(pos, key.size(), val);
            }
        };
        replace("{actor}",  actor_name);
        replace("{target}", target_name);
        return s;
    }

    // ─── 显示: TextBubble (raylib 中文字体, 无 ImGui) ─────
    inline void show_line_to_npc(Entity npc, const std::string& text) {
        if (!g_reg || !g_reg->is_alive(npc)) return;
        if (g_reg->has<TextBubble>(npc)) {
            g_reg->remove<TextBubble>(npc);
        }
        auto& tb = g_reg->emplace<TextBubble>(npc);
        std::strncpy(tb.text, text.c_str(), sizeof(tb.text) - 1);
        tb.text[sizeof(tb.text) - 1] = '\0';
        tb.display_time = 3.0f;
    }

    // ─── 内部: 不做 cooldown 检查的纯说话 ────────────────
    inline void do_say(Entity speaker, EventBus::EventType trigger, Entity related_actor) {
        if (!g_reg || !g_reg->is_alive(speaker)) return;

        auto need_opt = g_reg->try_get<NeedComponent>(speaker);
        auto emo_opt  = g_reg->try_get<EmotionComponent>(speaker);
        if (!need_opt.has_value() || !emo_opt.has_value()) return;

        auto idx = select_line(need_opt->get(), emo_opt->get(), trigger);
        if (!idx.has_value()) return;

        std::string actor_name = g_name_getter ? g_name_getter(related_actor) : "?";
        std::string text = instantiate(line_catalog[*idx].template_str,
                                       actor_name, "?");
        show_line_to_npc(speaker, text);
        last_speak_time[speaker.id] = current_time_acc;
    }

    inline bool in_cooldown(Entity npc) {
        auto it = last_speak_time.find(npc.id);
        if (it == last_speak_time.end()) return false;
        return current_time_acc - it->second < COOLDOWN_SEC;
    }

    // ─── 钩子 API ────────────────────────────────────────
    // 动作选定时: actor 说"动作之外显"——bypass cooldown (主要发声, 必须可见)
    inline void on_action_chosen(Entity actor, EventBus::EventType complete_event) {
        if (complete_event == EventBus::EventType::None) return;
        do_say(actor, complete_event, actor);
    }

    // witness 反应时: 受 cooldown 限制 (避免一波 witness 反应同时刷屏)
    inline void on_witness_react(Entity witness, EventBus::EventType event_type, Entity actor) {
        if (in_cooldown(witness)) return;
        do_say(witness, event_type, actor);
    }

    inline void Tick(float dt) {
        current_time_acc += dt;
    }

    inline void Init(Registry& reg, NameGetter getter) {
        g_reg = &reg;
        g_name_getter = getter;
        last_speak_time.clear();
        current_time_acc = 0.0f;
        // 注意: 不再订阅 EventBus 通配符. 由钩子驱动.
    }
}
