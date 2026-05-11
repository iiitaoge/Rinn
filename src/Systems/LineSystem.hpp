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

    // ─── 立场维度 (新增, 解决 NPC 自评攻击 / actor 误选 witness 台词) ───
    // SubjectKind 表达 "这条 line 在评价谁"；attitude 表达 "对那个 subject 是什么态度"。
    // speaker 对 subject 的好感（RelationComponent.affinity，自指=+1）与 attitude 同号则一致；
    // 反号则被惩罚——这就是村长不公开攻击村长、actor 不说 witness 台词的来源。
    // 软约束: consistency 连续, 在极端情绪下仍可冒头 (例如醉酒自嘲).
    enum class SubjectKind : uint8_t {
        NoSubject,      // 通用 / 不指涉任何具体人——跳过 stance 检查
        ActorOfEvent,   // 关于本次事件的 actor (覆盖绝大多数 line)
    };

    struct LineDef {
        const char* template_str;                                 // {actor} {target} 占位
        std::array<float, EmotionComponent::E> emotion_modulators;
        std::array<float, NeedComponent::N>    need_relevance;
        EventBus::EventType  trigger_event;                       // None = 通用候选
        float base_weight;
        SubjectKind subject_kind;   // 该 line 评价的对象类别
        float       attitude;       // -1..+1, 该 line 对 subject 的态度极性
    };

    inline Registry* g_reg = nullptr;
    inline float TEMPERATURE = 1.0f;
    inline float COOLDOWN_SEC = 4.0f;
    inline std::unordered_map<uint32_t, float> last_speak_time;
    inline float current_time_acc = 0.0f;

    // 名字 getter (LineSystem 不直接 include AIDebugUI; 由 main.cpp 注入)
    using NameGetter = std::string(*)(Entity);
    inline NameGetter g_name_getter = nullptr;

    // 34 行 catalog
    // 立场标注速查:
    //   AOE = ActorOfEvent (该 line 评价本次事件的 actor)
    //   NS  = NoSubject    (该 line 不指涉具体人, 跳过 stance 检查)
    //   attitude ∈ [-1, +1]: 对 subject 的态度极性 (负=敌意/批评, 正=认同/赞许)
    inline std::vector<LineDef> line_catalog = {
        // ═══ 加税触发 (TaxIncreased, actor = 加税者/村长) ═══
        // ↓ 受害者抱怨加税者 — 村长自指 -1×+1=-1 → 强压 (修 bug)
        { "我又被加税了！这是要逼死人吗？",         {1.5f,0.5f,0,0,0},     {1.5f,0,0,0,0,0},   EventBus::EventType::TaxIncreased, 1.0f, SubjectKind::ActorOfEvent, -1.0f },
        { "得藏点东西…不然撑不过这关",              {0,2.0f,0.5f,0,0},     {1.0f,0,0,1.0f,0,0},EventBus::EventType::TaxIncreased, 0.9f, SubjectKind::NoSubject,    0.0f },
        // ↓ 主修 bug: 村长自指 -1×+1=-1 → 强压, 无法选到
        { "天理何在！村长这是吸我们的血！",          {2.0f,0.5f,0,0.5f,0},  {1.0f,0,0,0,1.5f,0},EventBus::EventType::TaxIncreased, 0.8f, SubjectKind::ActorOfEvent, -1.0f },
        { "唉，又来。习惯了。",                     {0,0.3f,0,0.5f,0.3f},  {0.5f,0,0,0,0,0},   EventBus::EventType::TaxIncreased, 0.5f, SubjectKind::NoSubject,    0.0f },
        { "保佑村里平安渡过此劫…",                  {0,0.5f,0,0.5f,0.5f},  {0,0,0,0,2.0f,0},   EventBus::EventType::TaxIncreased, 0.7f, SubjectKind::NoSubject,    0.0f },
        // ↓ 为加税者辩护 — 村长自己说 +1×+0.7=+0.7 boost, 仇视村长的村民被压
        { "财政现实如此，加税在所难免。",           {0,0.5f,0,0,0},        {0,0,0,2.0f,0,0},   EventBus::EventType::TaxIncreased, 0.6f, SubjectKind::ActorOfEvent, +0.7f },

        // ═══ 神父之死 (PriestDied, actor = 神父) ═══
        { "神父走了…我们的精神支柱倒了",            {0,0,0.5f,1.5f,1.0f},  {0,0,0,0,2.0f,0},   EventBus::EventType::PriestDied, 1.0f, SubjectKind::ActorOfEvent, +0.8f },
        { "神父…他是村里最后的好人啊",              {0,0,0,2.0f,0.5f},     {0,0,1.0f,0,1.0f,0},EventBus::EventType::PriestDied, 0.9f, SubjectKind::ActorOfEvent, +1.0f },
        { "那个老头终于走了。可惜没人替他了。",     {0,0,0,1.0f,0.5f},     {0,1.0f,0,0,0.5f,0},EventBus::EventType::PriestDied, 0.4f, SubjectKind::ActorOfEvent, -0.5f },

        // ═══ 砸石碑 (BrokeDown, actor = 砸碑者) ═══
        // ↓ witness 谴责 actor — actor 自指 -0.8×+1=-0.8 → 强压 (修 actor 误选 witness 台词)
        { "{actor}疯了吗？砸了石碑！",              {0.5f,0,1.5f,0,0},     {0,0,0,1.0f,0,0},   EventBus::EventType::BrokeDown, 1.0f, SubjectKind::ActorOfEvent, -0.8f },
        { "这种人留在村里就是隐患…",                 {0.8f,0.5f,0,0,0},     {0,0,0,1.5f,0,0},   EventBus::EventType::BrokeDown, 0.7f, SubjectKind::ActorOfEvent, -0.8f },
        // ↓ actor 自述战斗呐喊 — actor 自指 +0.9×+1=+0.9 boost
        { "砸了它！这石碑就是吸血鬼！",              {3.0f,0,0,0,0},        {1.5f,0,0,0,0,0},   EventBus::EventType::BrokeDown, 1.2f, SubjectKind::ActorOfEvent, +0.9f },
        { "天哪…这下要出大事了",                    {0,1.5f,2.0f,0.5f,0},  {0,0,0,1.0f,1.0f,0},EventBus::EventType::BrokeDown, 0.9f, SubjectKind::NoSubject,    0.0f },

        // ═══ 拒税 (TaxRefused, actor = 拒税者) ═══
        { "{actor}居然敢拒税！要变天了？",          {0.5f,1.0f,1.5f,0,0},  {1.0f,0,0,1.0f,0,0},EventBus::EventType::TaxRefused, 1.0f, SubjectKind::NoSubject,    0.0f },
        { "我也想拒税，但是…我不敢",                 {0.5f,1.5f,2.0f,0.5f,0},{0,0,0,2.0f,0,0},  EventBus::EventType::TaxRefused, 0.6f, SubjectKind::NoSubject,    0.0f },
        // ↓ 赞拒税者: 喜欢拒税者的村民 boost; actor 自指 +1 也 boost
        { "他做对了！村长该尝尝苦头！",              {2.5f,0,0,0,0},        {1.0f,1.0f,0,0,0,0},EventBus::EventType::TaxRefused, 0.7f, SubjectKind::ActorOfEvent, +0.8f },
        { "我宁死也不交这血汗钱！",                  {2.5f,0,0,0,0},        {1.5f,0,0,0,0,0},   EventBus::EventType::TaxRefused, 1.1f, SubjectKind::ActorOfEvent, +1.0f },

        // ═══ 找村长理论 (ConfrontedLeader, actor = 对抗者) ═══
        { "{actor}去找村长理论了，不知道结果会怎样", {0,1.5f,0,0,0},        {0,1.5f,0,0,0,0},   EventBus::EventType::ConfrontedLeader, 0.9f, SubjectKind::NoSubject,    0.0f },
        // ↓ actor 自述对抗 — actor 自指 +1 boost
        { "村长，你这样做对得起村里人吗！",          {2.0f,0,0,0,0},        {0,2.0f,0,0,0,0},   EventBus::EventType::ConfrontedLeader, 1.0f, SubjectKind::ActorOfEvent, +0.7f },

        // ═══ 囤粮 (Hoarded, actor = 囤粮者) ═══
        { "{actor}也开始囤了…我也得囤点什么",        {0,2.0f,0.5f,0,0},     {1.0f,0,0,1.0f,0,0},EventBus::EventType::Hoarded, 0.8f, SubjectKind::NoSubject,    0.0f },
        { "得多藏点粮食…",                          {0,2.5f,0,0,0},        {1.0f,0,0,1.0f,0,0},EventBus::EventType::Hoarded, 1.0f, SubjectKind::NoSubject,    0.0f },

        // ═══ 私密恐慌动作 ═══
        { "把门栓上…谁也别进来",                    {0,0.5f,2.5f,0,0},     {0,0,0,1.5f,0,0},   EventBus::EventType::ClosedDoors, 1.0f, SubjectKind::NoSubject,    0.0f },
        { "我…我病了，不能见人",                    {0,0,2.0f,0.5f,0},     {0,0,0,1.0f,0,0},   EventBus::EventType::FeignedIllness, 1.0f, SubjectKind::NoSubject,    0.0f },
        // ↓ 拍马屁 — actor (拍马屁者) 自指 +0.7×+1=+0.7
        { "村长大人英明，我等愚民心服口服…",         {0,0,2.5f,0,0.5f},     {0,0,0,1.0f,0,0},   EventBus::EventType::Flattered, 1.0f, SubjectKind::ActorOfEvent, +0.7f },
        { "钱不是命，但藏起来才是命。",              {0,2.5f,0,0,0},        {1.5f,0,0,0,0,0},   EventBus::EventType::HidMoney, 1.0f, SubjectKind::NoSubject,    0.0f },
        { "先把矿工预付银子付了，免得手头没人用",    {0,2.0f,0,0,0},        {1.5f,0,0,1.0f,0,0},EventBus::EventType::PrepaidMiners, 1.0f, SubjectKind::NoSubject,    0.0f },

        // ═══ 接触型 (孤独动作) ═══
        { "老兄，今天能聊聊吗？",                   {0,0,0,0.5f,2.0f},     {0,1.5f,0,0,0,0},   EventBus::EventType::VisitedFriend, 1.0f, SubjectKind::NoSubject,    0.0f },
        { "去酒馆喝一杯，谁都别拦我",                {0,0,0,0.5f,2.5f},     {0,1.5f,0,0,0,0},   EventBus::EventType::WentToTavern, 1.0f, SubjectKind::NoSubject,    0.0f },
        { "我们一起送他最后一程吧…",                 {0,0,0,1.5f,2.0f},     {0,1.0f,1.0f,0,0,0},EventBus::EventType::AttendedFuneral, 1.0f, SubjectKind::NoSubject,    0.0f },

        // ═══ 悲伤系 ═══
        { "我心里堵得慌…谁能听我说说",               {0,0,0,2.0f,0.5f},     {0,0,2.0f,0,0,0},   EventBus::EventType::ConfidedGrief, 1.0f, SubjectKind::NoSubject,    0.0f },
        { "我的孩子…我的孩子啊…",                   {0,0,0,2.5f,1.0f},     {0,0,2.5f,0,0,0},   EventBus::EventType::SankIntoGrief, 1.0f, SubjectKind::NoSubject,    0.0f },
        { "愿苍天保佑我们这卑微的村庄",              {0,0,0,0.5f,0.5f},     {0,0,0,0,2.5f,0},   EventBus::EventType::Prayed, 1.0f, SubjectKind::NoSubject,    0.0f },

        // ═══ 通用低强度喃喃 (trigger = None) ═══
        { "唉…",                                    {0,0,0,1.5f,0.5f},     {0,0,1.0f,0,0,0},   EventBus::EventType::None, 0.3f, SubjectKind::NoSubject,    0.0f },
        { "这日子怎么过…",                          {0,1.5f,0.5f,0.5f,0},  {1.0f,0,0,1.0f,0,0},EventBus::EventType::None, 0.3f, SubjectKind::NoSubject,    0.0f },
    };

    // ─── 立场一致性辅助: speaker 对 subject 的 affinity 查询 (-100..+100) ──
    // 本地实现, 不依赖 AppraisalSystem.hpp, 避免循环耦合.
    inline int local_affinity(Entity from, Entity to) {
        if (!g_reg || from.is_null() || to.is_null()) return 0;
        for (Entity edge : g_reg->view<RelationComponent>()) {
            auto& r = g_reg->get<RelationComponent>(edge);
            if (r.from.id == from.id && r.to.id == to.id) return r.affinity;
        }
        return 0;  // 无边 = 中立
    }

    // 立场一致性因子: speaker 对 subject 的好感 × line 的 attitude
    // 同号 → consistency 正 → 轻度 boost; 反号 → 重压.
    // 自我指涉时 self-affinity 默认 +1, 这是「不公开自我攻击」的来源.
    // 软约束: 极端情绪下分数仍可能冒头 (例如醉酒自嘲), 此为 feature 非 bug.
    inline float stance_factor(Entity speaker, Entity event_actor, const LineDef& line) {
        if (line.subject_kind == SubjectKind::NoSubject) return 1.0f;

        Entity subject{};
        if (line.subject_kind == SubjectKind::ActorOfEvent) subject = event_actor;
        if (subject.is_null()) return 1.0f;

        float aff;
        if (speaker.id == subject.id) {
            aff = +1.0f;  // 自我立场默认正向
        } else {
            int raw = local_affinity(speaker, subject);
            aff = std::clamp(raw / 100.0f, -1.0f, 1.0f);
        }

        float consistency = line.attitude * aff;                       // -1..+1
        return std::clamp(0.6f + consistency * 0.5f, 0.1f, 1.2f);
        // consistency=+1 → 1.10 (轻 boost)
        // consistency= 0 → 0.60 (中性)
        // consistency=-1 → 0.10 (重压, 软, 不绝对)
    }

    // ─── 算分 (跟 DecisionSystem::compute_utility 同构) ───
    inline float compute_score(const NeedComponent& need,
                               const EmotionComponent& emo,
                               const LineDef& line,
                               Entity speaker,
                               Entity event_actor) {
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
        float stance = stance_factor(speaker, event_actor, line);
        return line.base_weight * emo_score * std::max(0.5f, need_score) * stance;
    }

    inline std::optional<size_t> select_line(const NeedComponent& need,
                                             const EmotionComponent& emo,
                                             EventBus::EventType trigger,
                                             Entity speaker,
                                             Entity event_actor) {
        std::vector<std::pair<float, size_t>> candidates;
        for (size_t i = 0; i < line_catalog.size(); ++i) {
            const auto& line = line_catalog[i];
            if (line.trigger_event != EventBus::EventType::None &&
                line.trigger_event != trigger) continue;
            float s = compute_score(need, emo, line, speaker, event_actor);
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

        auto idx = select_line(need_opt->get(), emo_opt->get(), trigger, speaker, related_actor);
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
