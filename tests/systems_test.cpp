#include <gtest/gtest.h>

#include "Core/Registry.hpp"
#include "Components/Components.hpp"
#include "Systems/ActionExecutionSystem.hpp"
#include "Systems/AppraisalSystem.hpp"
#include "Systems/CollisionSystem.hpp"
#include "Systems/DecisionSystem.hpp"
#include "Systems/EmotionDecaySystem.hpp"
#include "Systems/EntityNameSystem.hpp"
#include "Systems/EventSystem.hpp"
#include "Systems/LineSystem.hpp"
#include "Systems/PhysicSystem.hpp"
#include "UI/AIDebugUI.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>

using Rinn::Entity;
using Rinn::Registry;

namespace {

Rinn::NeedComponent make_need(float resource_gap = 0.0f,
                              float social_gap = 0.0f,
                              float family_gap = 0.0f,
                              float safety_gap = 0.0f,
                              float faith_gap = 0.0f,
                              float curiosity_gap = 0.0f) {
    Rinn::NeedComponent need{};
    need.weights.fill(1.0f);
    need.satisfaction.fill(0.0f);
    need.expectation = {
        resource_gap, social_gap, family_gap, safety_gap, faith_gap, curiosity_gap
    };
    return need;
}

Rinn::EmotionComponent make_emotion(float anger = 0.0f,
                                    float anxiety = 0.0f,
                                    float panic = 0.0f,
                                    float sadness = 0.0f,
                                    float loneliness = 0.0f) {
    Rinn::EmotionComponent emo{};
    emo.intensity = {anger, anxiety, panic, sadness, loneliness};
    emo.target.fill(Entity{});
    emo.decay_rate.fill(0.0f);
    return emo;
}

Rinn::DecisionComponent make_decision(uint16_t action_id = 0,
                                      float progress = 0.0f,
                                      uint32_t next_tick = 0) {
    Rinn::DecisionComponent dec{};
    dec.current_action_target = Entity{};
    dec.current_action_id = action_id;
    dec.action_progress = progress;
    dec.next_decision_tick = next_tick;
    return dec;
}

Rinn::StoneTabletComponent online_tablet() {
    Rinn::StoneTabletComponent tablet{};
    tablet.online = true;
    tablet.owner = Entity{};
    tablet.broadcast_range = 10;
    return tablet;
}

Rinn::RelationComponent relation(Entity from, Entity to, int affinity) {
    Rinn::RelationComponent r{};
    r.from = from;
    r.to = to;
    r.affinity = static_cast<int8_t>(affinity);
    r.power_diff = 0;
    return r;
}

void set_identity(Registry& reg, Entity e, const char* name, const char* display = "") {
    Rinn::IdentityComponent id{};
    std::strncpy(id.name, name, sizeof(id.name) - 1);
    std::strncpy(id.display_name, display, sizeof(id.display_name) - 1);
    (void)reg.emplace<Rinn::IdentityComponent>(e, id);
}

Entity make_ai_actor(Registry& reg,
                     Rinn::NeedComponent need = make_need(),
                     Rinn::EmotionComponent emo = make_emotion(),
                     Rinn::DecisionComponent dec = make_decision()) {
    Entity e = reg.create_entity();
    (void)reg.emplace<Rinn::NeedComponent>(e, need);
    (void)reg.emplace<Rinn::EmotionComponent>(e, emo);
    (void)reg.emplace<Rinn::DecisionComponent>(e, dec);
    return e;
}

Entity make_tablet_actor(Registry& reg,
                         Rinn::NeedComponent need = make_need(),
                         Rinn::EmotionComponent emo = make_emotion()) {
    Entity e = make_ai_actor(reg, need, emo);
    (void)reg.emplace<Rinn::StoneTabletComponent>(e, online_tablet());
    return e;
}

std::string test_name_getter(Entity e) {
    return std::string("E") + std::to_string(e.index());
}

class SystemsTest : public ::testing::Test {
protected:
    void SetUp() override {
        Rinn::EventBus::Clear();
        Rinn::CollisionSystem::grid.clear();
        Rinn::DecisionSystem::global_tick = 0;
        Rinn::DecisionSystem::g_on_action_chosen = nullptr;
        Rinn::DecisionSystem::last_scores.clear();
        Rinn::DecisionSystem::last_chosen.clear();
        Rinn::AppraisalSystem::g_reg = nullptr;
        Rinn::AppraisalSystem::fact_index.clear();
        Rinn::AppraisalSystem::g_on_witness_react = nullptr;
        Rinn::LineSystem::g_reg = nullptr;
        Rinn::LineSystem::g_name_getter = nullptr;
        Rinn::LineSystem::last_speak_time.clear();
        Rinn::LineSystem::current_time_acc = 0.0f;
        Rinn::EntityNameSystem::g_reg = nullptr;
        Rinn::EventLog::Clear();
    }

    void TearDown() override {
        Rinn::EventBus::Clear();
        Rinn::DecisionSystem::g_on_action_chosen = nullptr;
        Rinn::AppraisalSystem::g_on_witness_react = nullptr;
    }
};

} // namespace

TEST_F(SystemsTest, PhysicSystem_IntegratesOnlyEntitiesWithTransformAndVelocity) {
    Registry reg;
    Entity moving = reg.create_entity();
    Entity transform_only = reg.create_entity();
    Entity velocity_only = reg.create_entity();

    (void)reg.emplace<Rinn::Transform>(moving, Rinn::Transform{10.0f, 20.0f, 0});
    (void)reg.emplace<Rinn::Velocity>(moving, Rinn::Velocity{3.0f, -4.0f});
    (void)reg.emplace<Rinn::Transform>(transform_only, Rinn::Transform{100.0f, 200.0f, 0});
    (void)reg.emplace<Rinn::Velocity>(velocity_only, Rinn::Velocity{999.0f, 999.0f});

    Rinn::PhysicSystem::update(reg, 2.0f);

    EXPECT_FLOAT_EQ(reg.get<Rinn::Transform>(moving).x, 16.0f);
    EXPECT_FLOAT_EQ(reg.get<Rinn::Transform>(moving).y, 12.0f);
    EXPECT_FLOAT_EQ(reg.get<Rinn::Transform>(transform_only).x, 100.0f);
    EXPECT_FLOAT_EQ(reg.get<Rinn::Transform>(transform_only).y, 200.0f);
}

TEST_F(SystemsTest, CollisionSystem_DetectsOverlapsForDynamicEntities) {
    Registry reg;
    Entity dynamic = reg.create_entity();
    Entity wall = reg.create_entity();
    Entity far = reg.create_entity();

    (void)reg.emplace<Rinn::Transform>(dynamic, Rinn::Transform{0.0f, 0.0f, 0});
    (void)reg.emplace<Rinn::Collider>(dynamic, Rinn::Collider{10.0f, 10.0f});
    (void)reg.emplace<Rinn::Velocity>(dynamic, Rinn::Velocity{1.0f, 0.0f});
    (void)reg.emplace<Rinn::Transform>(wall, Rinn::Transform{5.0f, 0.0f, 0});
    (void)reg.emplace<Rinn::Collider>(wall, Rinn::Collider{10.0f, 10.0f});
    (void)reg.emplace<Rinn::Transform>(far, Rinn::Transform{200.0f, 0.0f, 0});
    (void)reg.emplace<Rinn::Collider>(far, Rinn::Collider{10.0f, 10.0f});

    auto hits = Rinn::CollisionSystem::detect(reg);

    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].a, dynamic);
    EXPECT_EQ(hits[0].b, wall);
}

TEST_F(SystemsTest, CollisionSystem_RespectsLayerMasks) {
    Rinn::Collider a{10.0f, 10.0f};
    Rinn::Collider b{10.0f, 10.0f};
    a.layer = 0x0001;
    a.mask = 0x0002;
    b.layer = 0x0004;
    b.mask = 0x0001;

    EXPECT_FALSE(Rinn::CollisionSystem::layers_match(a, b));

    b.layer = 0x0002;
    EXPECT_TRUE(Rinn::CollisionSystem::layers_match(a, b));
}

TEST_F(SystemsTest, CollisionSystem_ResolvePushesMovableEntityOutOfStaticCollider) {
    Registry reg;
    Entity movable = reg.create_entity();
    Entity wall = reg.create_entity();

    (void)reg.emplace<Rinn::Transform>(movable, Rinn::Transform{0.0f, 0.0f, 0});
    (void)reg.emplace<Rinn::Collider>(movable, Rinn::Collider{10.0f, 10.0f});
    (void)reg.emplace<Rinn::Velocity>(movable, Rinn::Velocity{1.0f, 0.0f});
    (void)reg.emplace<Rinn::Transform>(wall, Rinn::Transform{5.0f, 0.0f, 0});
    (void)reg.emplace<Rinn::Collider>(wall, Rinn::Collider{10.0f, 10.0f});

    Rinn::CollisionSystem::resolve(reg, {{movable, wall}});

    EXPECT_FLOAT_EQ(reg.get<Rinn::Transform>(movable).x, -5.0f);
    EXPECT_FLOAT_EQ(reg.get<Rinn::Transform>(wall).x, 5.0f);
}

TEST_F(SystemsTest, EmotionDecaySystem_DecaysAndClampsAtZero) {
    Registry reg;
    Entity e = reg.create_entity();
    Rinn::EmotionComponent emo = make_emotion(0.5f, 0.1f, 0.0f, 0.8f, 1.0f);
    emo.decay_rate = {0.1f, 0.2f, 0.3f, 1.0f, 0.0f};
    (void)reg.emplace<Rinn::EmotionComponent>(e, emo);

    Rinn::EmotionDecaySystem::Update(reg, 1.0f);

    const auto& out = reg.get<Rinn::EmotionComponent>(e);
    EXPECT_FLOAT_EQ(out.intensity[0], 0.4f);
    EXPECT_FLOAT_EQ(out.intensity[1], 0.0f);
    EXPECT_FLOAT_EQ(out.intensity[2], 0.0f);
    EXPECT_FLOAT_EQ(out.intensity[3], 0.0f);
    EXPECT_FLOAT_EQ(out.intensity[4], 1.0f);
}

TEST_F(SystemsTest, EventBus_DrainsTypedAndWildcardSubscribersInFifoOrder) {
    std::vector<int> calls;
    Rinn::EventBus::Subscribe(Rinn::EventBus::EventType::TaxIncreased,
        [&](const Rinn::EventBus::Event& e) { calls.push_back(e.payload_i); });
    Rinn::EventBus::SubscribeAll(
        [&](const Rinn::EventBus::Event& e) { calls.push_back(e.payload_i + 100); });

    Rinn::EventBus::Publish({Rinn::EventBus::EventType::TaxIncreased, {}, {}, 0.0f, 1});
    Rinn::EventBus::Publish({Rinn::EventBus::EventType::PriestDied, {}, {}, 0.0f, 2});
    Rinn::EventBus::Drain();

    EXPECT_EQ(calls, (std::vector<int>{1, 101, 102}));
    EXPECT_EQ(Rinn::EventBus::last_drain_count, 2u);
    EXPECT_EQ(Rinn::EventBus::total_published, 2u);
    EXPECT_FALSE(Rinn::EventBus::last_drain_overflow);
}

TEST_F(SystemsTest, EventBus_StopsDrainWhenCycleGuardTrips) {
    Rinn::EventBus::Subscribe(Rinn::EventBus::EventType::TaxIncreased,
        [](const Rinn::EventBus::Event& e) { Rinn::EventBus::Publish(e); });
    Rinn::EventBus::Publish({Rinn::EventBus::EventType::TaxIncreased});

    Rinn::EventBus::Drain();

    EXPECT_EQ(Rinn::EventBus::last_drain_count,
              static_cast<size_t>(Rinn::EventBus::MAX_DRAIN_PER_FRAME));
    EXPECT_TRUE(Rinn::EventBus::last_drain_overflow);
    EXPECT_FALSE(Rinn::EventBus::queue.empty());
}

TEST_F(SystemsTest, EntityNameSystem_UsesIdentityFallbacks) {
    Registry reg;
    Entity named = reg.create_entity();
    Entity display = reg.create_entity();
    Entity unnamed = reg.create_entity();
    set_identity(reg, named, "internal");
    set_identity(reg, display, "internal2", "Shown");
    Rinn::EntityNameSystem::Init(reg);

    EXPECT_EQ(Rinn::EntityNameSystem::NameOrId(Entity{}), "-");
    EXPECT_EQ(Rinn::EntityNameSystem::NameOrId(named), "internal");
    EXPECT_EQ(Rinn::EntityNameSystem::DisplayName(display), "Shown");
    EXPECT_EQ(Rinn::EntityNameSystem::DisplayName(named), "internal");
    EXPECT_EQ(Rinn::EntityNameSystem::NameOrId(unnamed), "E2");
}

TEST_F(SystemsTest, DecisionSystem_ComputesUtilityWithEmotionAndBias) {
    Rinn::NeedComponent need = make_need(1.0f);
    Rinn::EmotionComponent emo = make_emotion(0.5f);
    Rinn::DecisionSystem::ActionDef action{
        "test", 0, 2.0f, {2.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        1.0f, Rinn::EventBus::EventType::None,
        Rinn::DecisionSystem::TargetKind::NoTarget
    };
    Rinn::ActionBiasComponent bias{};
    bias.bias.fill(0.0f);
    bias.bias[3] = 0.25f;

    float utility = Rinn::DecisionSystem::compute_utility(need, emo, action, &bias, 3);

    EXPECT_FLOAT_EQ(utility, 4.25f);
}

TEST_F(SystemsTest, DecisionSystem_SelectsBestActionRecordsScoresAndCallsHook) {
    Registry reg;
    Entity actor = make_ai_actor(reg, make_need(1.0f), make_emotion(0.5f), make_decision(0, 0.5f, 0));
    Rinn::ActionBiasComponent bias{};
    bias.bias.fill(0.0f);
    bias.bias[1] = 10.0f;
    (void)reg.emplace<Rinn::ActionBiasComponent>(actor, bias);

    static int hook_calls = 0;
    static Entity hook_actor;
    static Rinn::EventBus::EventType hook_event;
    hook_calls = 0;
    hook_actor = Entity{};
    hook_event = Rinn::EventBus::EventType::None;
    Rinn::DecisionSystem::g_on_action_chosen =
        [](Entity e, Rinn::EventBus::EventType ev) {
            ++hook_calls;
            hook_actor = e;
            hook_event = ev;
        };

    Rinn::DecisionSystem::Update(reg, 0.0f);

    auto& dec = reg.get<Rinn::DecisionComponent>(actor);
    EXPECT_EQ(dec.current_action_id, 1u);
    EXPECT_FLOAT_EQ(dec.action_progress, 0.0f);
    EXPECT_EQ(Rinn::DecisionSystem::last_chosen[actor.id], 1);
    ASSERT_EQ(Rinn::DecisionSystem::last_scores[actor.id].size(),
              Rinn::DecisionSystem::action_catalog.size());
    EXPECT_EQ(hook_calls, 1);
    EXPECT_EQ(hook_actor, actor);
    EXPECT_EQ(hook_event, Rinn::EventBus::EventType::BrokeDown);
}

TEST_F(SystemsTest, DecisionSystem_SkipsUntilNextDecisionTick) {
    Registry reg;
    Entity actor = make_ai_actor(reg, make_need(1.0f), make_emotion(), make_decision(0, 0.5f, 10));

    Rinn::DecisionSystem::Update(reg, 0.0f);

    EXPECT_EQ(reg.get<Rinn::DecisionComponent>(actor).current_action_id, 0u);
    EXPECT_FLOAT_EQ(reg.get<Rinn::DecisionComponent>(actor).action_progress, 0.5f);
    EXPECT_TRUE(Rinn::DecisionSystem::last_chosen.find(actor.id) ==
                Rinn::DecisionSystem::last_chosen.end());
}

TEST_F(SystemsTest, DecisionSystem_RequestRedecideSetsNextTickToZero) {
    Registry reg;
    Entity actor = make_ai_actor(reg, make_need(), make_emotion(), make_decision(0, 0.0f, 99));

    Rinn::DecisionSystem::RequestRedecide(reg, actor);

    EXPECT_EQ(reg.get<Rinn::DecisionComponent>(actor).next_decision_tick, 0u);
}

TEST_F(SystemsTest, ActionExecutionSystem_CompletesActionSatisfiesNeedAndPublishesEvent) {
    Registry reg;
    Rinn::EntityNameSystem::Init(reg);
    Entity actor = make_ai_actor(reg, make_need(1.0f), make_emotion(), make_decision(1, 0.0f, 99));
    Entity leader = reg.create_entity();
    (void)reg.emplace<Rinn::IsLeader>(leader);

    Rinn::ActionExecutionSystem::Update(reg, 8.0f);

    const auto& dec = reg.get<Rinn::DecisionComponent>(actor);
    EXPECT_FLOAT_EQ(dec.action_progress, 1.0f);
    EXPECT_EQ(dec.next_decision_tick, 0u);
    EXPECT_FLOAT_EQ(reg.get<Rinn::NeedComponent>(actor).satisfaction[0], 0.30f);
    ASSERT_EQ(Rinn::EventBus::queue.size(), 1u);
    auto event = Rinn::EventBus::queue.front();
    EXPECT_EQ(event.type, Rinn::EventBus::EventType::BrokeDown);
    EXPECT_EQ(event.actor, actor);
    EXPECT_TRUE(event.target.is_null());
    EXPECT_EQ(Rinn::EventLog::count, 1u);
}

TEST_F(SystemsTest, ActionExecutionSystem_ResolvesLeaderTarget) {
    Registry reg;
    Entity actor = make_ai_actor(reg, make_need(1.0f), make_emotion(), make_decision(2, 0.0f, 99));
    Entity leader = reg.create_entity();
    (void)reg.emplace<Rinn::IsLeader>(leader);

    Rinn::ActionExecutionSystem::Update(reg, 6.0f);

    ASSERT_EQ(Rinn::EventBus::queue.size(), 1u);
    auto event = Rinn::EventBus::queue.front();
    EXPECT_EQ(event.type, Rinn::EventBus::EventType::TaxRefused);
    EXPECT_EQ(event.target, leader);
}

TEST_F(SystemsTest, AppraisalSystem_InformationEventCreatesFactAndUpdatesOnlineKnowers) {
    Registry reg;
    Entity actor = make_tablet_actor(reg, make_need(1.0f), make_emotion());
    Entity witness = make_tablet_actor(reg, make_need(1.0f), make_emotion());
    Entity offline = make_ai_actor(reg, make_need(1.0f), make_emotion());
    (void)reg.emplace<Rinn::StoneTabletComponent>(offline, Rinn::StoneTabletComponent{false, {}, 10});
    Rinn::AppraisalSystem::Init(reg);

    Rinn::EventBus::Publish({Rinn::EventBus::EventType::TaxIncreased, actor, {}, 1.0f, 0});
    Rinn::EventBus::Drain();

    auto fact = Rinn::AppraisalSystem::get_fact(Rinn::EventBus::EventType::TaxIncreased);
    ASSERT_TRUE(fact.has_value());
    const auto& data = reg.get<Rinn::KnowledgeFactComponent>(*fact);
    EXPECT_EQ(data.subject, actor);
    EXPECT_TRUE(data.knowers.test(actor.index()));
    EXPECT_TRUE(data.knowers.test(witness.index()));
    EXPECT_FALSE(data.knowers.test(offline.index()));
    EXPECT_GT(reg.get<Rinn::EmotionComponent>(witness).intensity[0], 0.0f);
    EXPECT_GT(reg.get<Rinn::EmotionComponent>(witness).intensity[1], 0.0f);
    EXPECT_EQ(reg.get<Rinn::DecisionComponent>(witness).next_decision_tick, 0u);
}

TEST_F(SystemsTest, AppraisalSystem_TaxRefusedAffectsWitnessLeaderAndAffinity) {
    Registry reg;
    Entity actor = make_tablet_actor(reg, make_need(), make_emotion());
    Entity leader = make_tablet_actor(reg, make_need(), make_emotion());
    Entity witness = make_tablet_actor(reg, make_need(), make_emotion());
    Entity rel_edge = reg.create_entity();
    (void)reg.emplace<Rinn::RelationComponent>(rel_edge, relation(witness, actor, 0));
    Rinn::AppraisalSystem::Init(reg);

    Rinn::EventBus::Publish({Rinn::EventBus::EventType::TaxRefused, actor, leader, 0.0f, 0});
    Rinn::EventBus::Drain();

    EXPECT_FLOAT_EQ(reg.get<Rinn::EmotionComponent>(actor).intensity[0], 0.0f);
    EXPECT_FLOAT_EQ(reg.get<Rinn::EmotionComponent>(leader).intensity[0], 0.45f);
    EXPECT_FLOAT_EQ(reg.get<Rinn::EmotionComponent>(leader).intensity[2], 0.0f);
    EXPECT_GT(reg.get<Rinn::EmotionComponent>(witness).intensity[2], 0.0f);
    EXPECT_EQ(reg.get<Rinn::RelationComponent>(rel_edge).affinity, -3);
}

TEST_F(SystemsTest, AppraisalSystem_BumpEmotionClampsAndInvalidIndexNoOps) {
    auto emo = make_emotion(0.95f);

    Rinn::AppraisalSystem::bump_emotion(emo, 0, 0.50f);
    Rinn::AppraisalSystem::bump_emotion(emo, 1, -0.50f);
    Rinn::AppraisalSystem::bump_emotion(emo, -1, 1.0f);
    Rinn::AppraisalSystem::bump_emotion(emo, Rinn::EmotionComponent::E, 1.0f);

    EXPECT_FLOAT_EQ(emo.intensity[0], 1.0f);
    EXPECT_FLOAT_EQ(emo.intensity[1], 0.0f);
}

TEST_F(SystemsTest, LineSystem_InstantiateReplacesActorAndTargetPlaceholders) {
    auto text = Rinn::LineSystem::instantiate("{actor} greets {target}; {actor} stays", "Alice", "Bob");

    EXPECT_EQ(text, "Alice greets Bob; Alice stays");
}

TEST_F(SystemsTest, LineSystem_ShowLineCreatesOrReplacesTextBubble) {
    Registry reg;
    Entity npc = reg.create_entity();
    Rinn::LineSystem::Init(reg, test_name_getter);

    Rinn::LineSystem::show_line_to_npc(npc, "first");
    Rinn::LineSystem::show_line_to_npc(npc, "second");

    ASSERT_TRUE(reg.has<Rinn::TextBubble>(npc));
    const auto& bubble = reg.get<Rinn::TextBubble>(npc);
    EXPECT_STREQ(bubble.text, "second");
    EXPECT_FLOAT_EQ(bubble.display_time, 3.0f);
}

TEST_F(SystemsTest, LineSystem_CooldownBlocksWitnessButActionBypassesCooldown) {
    Registry reg;
    Entity npc = make_ai_actor(reg, make_need(1.0f), make_emotion(1.0f));
    Rinn::LineSystem::Init(reg, test_name_getter);

    Rinn::LineSystem::show_line_to_npc(npc, "existing");
    Rinn::LineSystem::last_speak_time[npc.id] = 0.0f;
    Rinn::LineSystem::current_time_acc = 1.0f;

    Rinn::LineSystem::on_witness_react(npc, Rinn::EventBus::EventType::BrokeDown, npc);
    EXPECT_STREQ(reg.get<Rinn::TextBubble>(npc).text, "existing");

    Rinn::LineSystem::on_action_chosen(npc, Rinn::EventBus::EventType::BrokeDown);
    EXPECT_STRNE(reg.get<Rinn::TextBubble>(npc).text, "existing");
}

TEST_F(SystemsTest, LineSystem_StanceFactorRewardsAlignedAttitudeAndPenalizesOpposition) {
    Registry reg;
    Entity speaker = reg.create_entity();
    Entity actor = reg.create_entity();
    Entity rel_edge = reg.create_entity();
    (void)reg.emplace<Rinn::RelationComponent>(rel_edge, relation(speaker, actor, 100));
    Rinn::LineSystem::Init(reg, test_name_getter);

    Rinn::LineSystem::LineDef positive{
        "positive", {}, {}, Rinn::EventBus::EventType::BrokeDown,
        1.0f, Rinn::LineSystem::SubjectKind::ActorOfEvent, 1.0f
    };
    Rinn::LineSystem::LineDef negative = positive;
    negative.attitude = -1.0f;

    EXPECT_GT(Rinn::LineSystem::stance_factor(speaker, actor, positive),
              Rinn::LineSystem::stance_factor(speaker, actor, negative));
    EXPECT_FLOAT_EQ(Rinn::LineSystem::stance_factor(speaker, actor, negative), 0.1f);
}
