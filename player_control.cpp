/* Copyright (C) 2013-2014 Michal Brzozowski (rusolis@poczta.fm)

   This file is part of KeeperRL.

   KeeperRL is free software; you can redistribute it and/or modify it under the terms of the
   GNU General Public License as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   KeeperRL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program.
   If not, see http://www.gnu.org/licenses/ . */

#include "stdafx.h"

#include "player_control.h"
#include "level.h"
#include "task.h"
#include "model.h"
#include "statistics.h"
#include "options.h"
#include "technology.h"
#include "util.h"
#include "village_control.h"
#include "item.h"
#include "item_factory.h"
#include "creature.h"
#include "square.h"
#include "view_id.h"
#include "collective.h"
#include "effect.h"
#include "music.h"
#include "encyclopedia.h"
#include "map_memory.h"
#include "item_action.h"
#include "equipment.h"
#include "collective_teams.h"
#include "minion_equipment.h"
#include "task_map.h"
#include "construction_map.h"
#include "minion_activity_map.h"
#include "spell.h"
#include "tribe.h"
#include "visibility_map.h"
#include "creature_name.h"
#include "view.h"
#include "view_index.h"
#include "collective_attack.h"
#include "territory.h"
#include "sound.h"
#include "game.h"
#include "collective_name.h"
#include "creature_attributes.h"
#include "collective_config.h"
#include "villain_type.h"
#include "workshops.h"
#include "attack_trigger.h"
#include "view_object.h"
#include "body.h"
#include "furniture.h"
#include "furniture_type.h"
#include "furniture_factory.h"
#include "known_tiles.h"
#include "zones.h"
#include "inventory.h"
#include "immigration.h"
#include "scroll_position.h"
#include "tutorial.h"
#include "tutorial_highlight.h"
#include "container_range.h"
#include "collective_warning.h"
#include "furniture_usage.h"
#include "message_generator.h"
#include "message_buffer.h"
#include "minion_controller.h"
#include "build_info.h"
#include "vision.h"
#include "external_enemies.h"
#include "resource_info.h"
#include "workshop_item.h"
#include "time_queue.h"
#include "unknown_locations.h"
#include "furniture_click.h"
#include "campaign.h"
#include "game_event.h"
#include "view_object_action.h"
#include "storage_id.h"
#include "fx_name.h"
#include "content_factory.h"
#include "tech_id.h"
#include "pretty_printing.h"
#include "game_save_type.h"
#include "immigrant_info.h"
#include "special_trait.h"
#include "user_input.h"
#include "automaton_part.h"
#include "shortest_path.h"
#include "scripted_ui_data.h"
#include "item_types.h"
#include "furnace.h"
#include "promotion_info.h"

template <class Archive>
void PlayerControl::serialize(Archive& ar, const unsigned int version) {
  ar& SUBCLASS(CollectiveControl) & SUBCLASS(EventListener);
  ar(memory, introText, nextKeeperWarning, tribeAlignment);
  ar(newAttacks, notifiedAttacks, messages, hints, soloKeeper);
  ar(visibilityMap, unknownLocations, dismissedVillageInfos, buildInfo, battleSummary);
  ar(messageHistory, tutorial, controlModeMessages, stunnedCreatures, usedResources, allianceAttack);
}

SERIALIZABLE(PlayerControl)

SERIALIZATION_CONSTRUCTOR_IMPL(PlayerControl)

using ResourceId = Collective::ResourceId;

const LocalTime hintFrequency = LocalTime(700);
static vector<TString> getHints() {
  return {
    TStringId("MINION_MORALE_HINT"),
 //   "You can turn these hints off in the settings (F2).",
    TStringId("KILLING_LEADER_HINT"),
//    "Your minions' morale is boosted when they are commanded by the Keeper.",
  };
}

PlayerControl::PlayerControl(Private, Collective* col, TribeAlignment alignment)
    : CollectiveControl(col), hints(getHints()), tribeAlignment(alignment) {
  controlModeMessages = make_shared<MessageBuffer>();
  visibilityMap = make_shared<VisibilityMap>();
  unknownLocations = make_shared<UnknownLocations>();
  memory.reset(new MapMemory());
  for (auto pos : col->getModel()->getGroundLevel()->getAllPositions())
    if (auto f = pos.getFurniture(FurnitureLayer::MIDDLE))
      if (f->isClearFogOfWar())
        addToMemory(pos);
}

PPlayerControl PlayerControl::create(Collective* col, vector<TString> introText, TribeAlignment alignment) {
  auto ret = makeOwner<PlayerControl>(Private{}, col, alignment);
  ret->subscribeTo(col->getModel());
  ret->introText = introText;
  return ret;
}

PlayerControl::~PlayerControl() {
}

static TString getPopulationIncreaseDescription(const Furniture::PopulationInfo& info, const TString& populationString) {
  if (info.limit)
    return TSentence("INCREASES_POP_LIMIT_BY_UP_TO", {populationString, TString(info.increase), TString(*info.limit)});
  else
    return TSentence("INCREASES_POP_LIMIT_BY", populationString, TString(info.increase));
}

void PlayerControl::loadBuildingMenu(const ContentFactory* contentFactory, const KeeperCreatureInfo& keeperCreatureInfo) {
  for (auto& group : keeperCreatureInfo.buildingGroups)
    buildInfo.append(contentFactory->buildInfo.at(group));
  auto factory = getGame()->getContentFactory();
  for (auto& info : buildInfo)
    if (auto furniture = info.type.getReferenceMaybe<BuildInfoTypes::Furniture>()) {
      usedResources.insert(furniture->cost.id);
      for (auto type : furniture->types) {
        double luxury = factory->furniture.getData(type).getLuxury();
        if (luxury > 0) {
          info.help = TSentence("COMBINE_SENTENCES", std::move(info.help),
              TSentence("INCREASES_LUXURY_BY", TString(luxury)));
          break;
        }
      }
      auto increase = factory->furniture.getData(furniture->types[0]).getPopulationIncrease();
      if (increase.increase > 0) {
        info.help = TSentence("COMBINE_SENTENCES", std::move(info.help),
            getPopulationIncreaseDescription(increase, keeperCreatureInfo.populationString));
        if (increase.limit)
          const_cast<optional<int>&>(furniture->limit) = int(*increase.limit / increase.increase);
      }
      auto& maxTraining = factory->furniture.getData(furniture->types[0]).getMaxTraining();
      for (auto& elem : maxTraining)
        info.help = TSentence("COMBINE_SENTENCES", std::move(info.help),
            TSentence("ADDS_TRAINING_LEVELS", TString(elem.second), factory->attrInfo.at(elem.first).name));
    }
}

void PlayerControl::loadImmigrationAndWorkshops(ContentFactory* contentFactory,
    const KeeperCreatureInfo& keeperCreatureInfo) {
  Technology technology = contentFactory->technology;
  for (auto& tech : copyOf(technology.techs))
    if (!keeperCreatureInfo.technology.contains(tech.first))
      technology.techs.erase(tech.first);
  WorkshopArray merged;
  for (auto& group : keeperCreatureInfo.workshopGroups)
    for (auto& elem : contentFactory->workshopGroups.at(group))
      for (auto& workshopItem : elem.second)
        if (!workshopItem.tech || technology.techs.count(*workshopItem.tech))
          merged[elem.first].push_back(workshopItem);
  collective->setWorkshops(make_unique<Workshops>(std::move(merged), contentFactory));
  for (auto& workshop : collective->getWorkshops().types)
    for (auto& option : workshop.second.getOptions())
      usedResources.insert(option.cost.id);
  vector<ImmigrantInfo> immigrants;
  for (auto elem : keeperCreatureInfo.immigrantGroups)
    append(immigrants, contentFactory->immigrantsData.at(elem));
  for (auto& elem : immigrants)
    for (auto& req : elem.requirements)
      if (auto cost = req.type.getReferenceMaybe<CostInfo>())
        usedResources.insert(cost->id);
  CollectiveConfig::addBedRequirementToImmigrants(immigrants, contentFactory);
  collective->setImmigration(makeOwner<Immigration>(collective, std::move(immigrants)));
  collective->setTechnology(std::move(technology));
  loadBuildingMenu(contentFactory, keeperCreatureInfo);
}

const vector<Creature*>& PlayerControl::getControlled() const {
  return getGame()->getPlayerCreatures();
}

optional<TeamId> PlayerControl::getCurrentTeam() const {
  for (TeamId team : getTeams().getAllActive())
    if (getTeams().getLeader(team)->isPlayer())
      return team;
  return none;
}

void PlayerControl::onControlledKilled(Creature* victim) {
  TeamId currentTeam = *getCurrentTeam();
  if (getTeams().getLeader(currentTeam) == victim && getGame()->getPlayerCreatures().size() == 1) {
    vector<PlayerInfo> team;
    for (auto c : getTeams().getMembers(currentTeam))
      if (c != victim)
        team.push_back(PlayerInfo(c, getGame()->getContentFactory()));
    optional<Creature::Id> newLeader;
    if (team.size() == 1)
      newLeader = team[0].creatureId;
    else if (!team.empty())
      newLeader = getView()->chooseCreature(TStringId("CHOOSE_NEW_TEAM_LEADER"), team,
          TStringId("ORDER_TEAM_BACK_TO_BASE"));
    if (newLeader) {
      if (Creature* c = getCreature(*newLeader)) {
        getTeams().setLeader(currentTeam, c);
        if (!c->isPlayer())
          c->pushController(createMinionController(c));
        if (victim->isPlayer())
          victim->popController();
        return;
      }
    }
    leaveControl();
  }
  if (victim->isPlayer())
    victim->popController();
}

void PlayerControl::onSunlightVisibilityChanged() {
  auto& f = getGame()->getContentFactory()->furniture;
  for (auto type : f.getAllFurnitureType())
    if (f.getData(type).isEyeball())
      for (auto pos : collective->getConstructions().getBuiltPositions(type))
        visibilityMap->updateEyeball(pos);
}

void PlayerControl::setTutorial(STutorial t) {
  tutorial = t;
}

STutorial PlayerControl::getTutorial() const {
  return tutorial;
}

bool PlayerControl::canControlSingle(const Creature* c) const {
  return !collective->hasTrait(c, MinionTrait::PRISONER) && !c->isAffected(LastingEffect::TURNED_OFF);
}

bool PlayerControl::canControlInTeam(const Creature* c) const {
  return (collective->hasTrait(c, MinionTrait::FIGHTER) || collective->hasTrait(c, MinionTrait::LEADER)) &&
      !c->isAffected(LastingEffect::STEED);
}

void PlayerControl::addToCurrentTeam(Creature* c) {
  collective->freeTeamMembers({c});
  if (auto teamId = getCurrentTeam()) {
    getTeams().add(*teamId, c);
    if (getGame()->getPlayerCreatures().size() > 1)
      c->pushController(createMinionController(c));
  }
}

void PlayerControl::teamMemberAction(TeamMemberAction action, Creature::Id id) {
  if (Creature* c = getCreature(id))
    switch (action) {
      case TeamMemberAction::MOVE_NOW:
        c->getPosition().getModel()->getTimeQueue().moveNow(c);
        break;
      case TeamMemberAction::CHANGE_LEADER:
        if (auto teamId = getCurrentTeam())
          if (getTeams().getMembers(*teamId).size() > 1 && canControlInTeam(c)) {
            auto controlled = getControlled();
            if (controlled.size() == 1) {
              getTeams().getLeader(*teamId)->popController();
              getTeams().setLeader(*teamId, c);
              c->pushController(createMinionController(c));
            }
          }
        break;
      case TeamMemberAction::REMOVE_MEMBER: {
        auto& teams = getTeams();
        if (auto teamId = getCurrentTeam())
          if (teams.getMembers(*teamId).size() > 1 && teams.contains(*teamId, c)) {
            teams.remove(*teamId, c);
            if (c->isPlayer()) {
              c->popController();
              auto newLeader = teams.getLeader(*teamId);
              if (!newLeader->isPlayer())
                newLeader->pushController(createMinionController(newLeader));
            }
          }
        break;
      }
    }
}

static ScriptedUIDataElems::Record getCreatureRecord(const ContentFactory* factory, const Creature* creature) {
  auto bestAttack = creature->getBestAttack(factory);
  return ScriptedUIDataElems::Record {{
      {"view_id", creature->getViewObject().getViewIdList()},
      {"attack_view_id", ViewIdList{bestAttack.viewId}},
      {"attack_value", TString(toString(bestAttack.value))},
      {"name", capitalFirst(creature->getName().aOrTitle())},
  }};
}

void PlayerControl::presentMinionsFreedMessage(const vector<Creature*>& creatures) {
  auto data = ScriptedUIDataElems::List{};
  auto factory = getGame()->getContentFactory();
  for (auto c : creatures)
    data.push_back(getCreatureRecord(factory, c));
  getView()->scriptedUI("prisoners_freed", data);
}

void PlayerControl::presentAndClearBattleSummary() {
  auto data = ScriptedUIDataElems::Record{};
  auto factory = getGame()->getContentFactory();
  auto getCreatureList = [factory] (const vector<Creature*>& creatures) {
    auto ret = ScriptedUIDataElems::List{};
    for (auto c : creatures)
      ret.push_back(getCreatureRecord(factory, c));
    return ret;
  };
  if (!battleSummary.minionsKilled.empty())
    data.elems["minions_killed"] = getCreatureList(battleSummary.minionsKilled);
  if (!battleSummary.minionsCaptured.empty())
    data.elems["minions_captured"] = getCreatureList(battleSummary.minionsCaptured);
  if (!battleSummary.enemiesKilled.empty())
    data.elems["enemies_killed"] = getCreatureList(battleSummary.enemiesKilled);
  if (!battleSummary.enemiesCaptured.empty())
    data.elems["enemies_captured"] = getCreatureList(battleSummary.enemiesCaptured);
  if (!data.elems.empty())
    getView()->scriptedUI("battle_summary", data);
  battleSummary = BattleSummary {};
}

void PlayerControl::leaveControl() {
  set<TeamId> allTeams;
  for (auto controlled : copyOf(getControlled())) {
    if (collective->hasTrait(controlled, MinionTrait::LEADER))
      nextKeeperWarning = collective->getGlobalTime();
    auto controlledLevel = controlled->getPosition().getLevel();
    if (!!getModel()->getMainLevelDepth(controlledLevel))
      setScrollPos(controlled->getPosition());
    controlled->popController();
    for (TeamId team : getTeams().getActive(controlled))
      allTeams.insert(team);
  }
  for (auto team : allTeams) {
    // a creature may die when landing and be removed from the team so copy the members vector
    for (Creature* c : copyOf(getTeams().getMembers(team)))
      // c might be dead if it was just killed which caused a crash if it was riding a steed
      if (!c->isDead() && c->getPosition().getModel() != getModel() && !c->isAffected(LastingEffect::STUNNED)) {
        getGame()->transferCreature(c, getModel());
        if (c->isAffected(LastingEffect::RIDER))
          if (auto steed = collective->getSteedOrRider(c))
            c->forceMount(steed);
      }
    if (getTeams().getMembers(team).size() == 1)
      getTeams().cancel(team);
    else
      getTeams().deactivate(team);
    break;
  }
  getView()->stopClock();
  presentAndClearBattleSummary();
}

void PlayerControl::render(View* view) {
  PROFILE;
  //collective->getConstructions().checkDebtConsistency();
  if (getControlled().empty()) {
    view->updateView(this, false);
  }
  if (firstRender) {
    firstRender = false;
    for (Creature* c : getCreatures())
      updateMinionVisibility(c);
    if (getGame()->getOptions()->getBoolValue(OptionId::CONTROLLER_HINT_REAL_TIME)) {
      getView()->scriptedUI("controller_hint_real_time", ScriptedUIData{});
      getGame()->getOptions()->setValue(OptionId::CONTROLLER_HINT_REAL_TIME, 0);
    }
  }
  if (!tutorial && !introText.empty() && getGame()->getOptions()->getBoolValue(OptionId::HINTS)) {
    view->updateView(this, false);
    for (auto& msg : introText)
      view->presentText(none, msg);
    introText.clear();
  }
}

void PlayerControl::addConsumableItem(Creature* creature) {
  ScriptedUIState state;
  while (1) {
    Item* chosenItem = chooseEquipmentItem(creature, {}, [&](const Item* it) {
        return !collective->getMinionEquipment().isOwner(it, creature)
            && !it->canEquip()
            && collective->getMinionEquipment().needsItem(creature, it, true); }, &state);
    if (chosenItem) {
      creature->removeEffect(LastingEffect::SLEEP);
      CHECK(collective->getMinionEquipment().tryToOwn(creature, chosenItem));
    } else
      break;
  }
}

void PlayerControl::addEquipment(Creature* creature, EquipmentSlot slot) {
  vector<Item*> currentItems = creature->getEquipment().getSlotItems(slot);
  Item* chosenItem = chooseEquipmentItem(creature, currentItems, [&](const Item* it) {
      return !collective->getMinionEquipment().isOwner(it, creature)
      && creature->canEquipIfEmptySlot(it, nullptr) && it->getEquipmentSlot() == slot; });
  if (chosenItem) {
    if (!chosenItem->getAutoEquipPredicate().apply(creature, nullptr) &&
        !getView()->yesOrNoPrompt(chosenItem->getEquipWarning()))
      return;
    vector<Item*> conflictingItems;
    for (auto it : collective->getMinionEquipment().getItemsOwnedBy(creature))
      if (it->isConflictingEquipment(chosenItem))
        conflictingItems.push_back(it);
    if (getView()->confirmConflictingItems(getGame()->getContentFactory(), conflictingItems)) {
      if (auto creatureId = collective->getMinionEquipment().getOwner(chosenItem))
        if (Creature* c = getCreature(*creatureId))
          c->removeEffect(LastingEffect::SLEEP);
      creature->removeEffect(LastingEffect::SLEEP);
      CHECK(collective->getMinionEquipment().tryToOwn(creature, chosenItem));
    }
  }
}

void PlayerControl::minionEquipmentAction(const EquipmentActionInfo& action) {
  if (auto creature = getCreature(action.creature))
    switch (action.action) {
      case ItemAction::DROP_STEED:
        collective->setSteed(creature, nullptr);
        break;
      case ItemAction::DROP:
        for (auto id : action.ids)
          collective->getMinionEquipment().discard(id);
        break;
      case ItemAction::REPLACE_STEED:
        if (auto steed = chooseSteed(creature, getCreatures().filter(
            [creature](Creature* c) { return c->isAffected(LastingEffect::STEED) && creature->isSteedGoodSize(c); })))
          collective->setSteed(creature, steed);
        break;
      case ItemAction::REPLACE:
        if (action.slot)
          addEquipment(creature, *action.slot);
        else
          addConsumableItem(creature);
        break;
      case ItemAction::LOCK:
        /*if (action.ids.empty() && action.slot)
          collective->getMinionEquipment().toggleLocked(creature, *action.slot);
        else*/
          for (auto id : action.ids)
            collective->getMinionEquipment().toggleLocked(creature, id);
        break;
      default:
        break;
    }
}

void PlayerControl::minionAIAction(const AIActionInfo& action) {
  if (auto c = getCreature(action.creature)) {
    c->getAttributes().setAIType(action.switchTo);
    if (action.override)
      for (auto other : getMinionGroup(action.groupName))
        other->getAttributes().setAIType(action.switchTo);
  }
}

void PlayerControl::minionTaskAction(const TaskActionInfo& action) {
  auto considerResettingActivity = [this] (Creature* c) {
    if (!collective->isActivityGoodAssumingHaveTasks(c, collective->getCurrentActivity(c).activity))
      collective->setMinionActivity(c, MinionActivity::IDLE);
  };
  if (auto c = getCreature(action.creature)) {
    if (action.switchTo)
      collective->setMinionActivity(c, *action.switchTo);
    for (MinionActivity task : action.lock)
      c->getAttributes().getMinionActivities().toggleLock(task);
    collective->flipGroupLockedActivities(action.groupName, action.lockGroup);
    if (!action.lock.isEmpty())
      considerResettingActivity(c);
  }
  if (!action.lockGroup.isEmpty())
    for (auto c : collective->getCreatures())
      considerResettingActivity(c);
}

void PlayerControl::equipmentGroupAction(const EquipmentGroupAction& action) {
  auto& groupSet = collective->lockedEquipmentGroups[action.group];
  for (auto& elem : action.flip)
    if (groupSet.count(elem))
      groupSet.erase(elem);
    else
      groupSet.insert(elem);
}

static ItemInfo getItemInfo(const ContentFactory* factory, const vector<Item*>& stack, bool equiped, bool pending, bool locked,
    optional<ItemInfo::Type> type = none) {
  PROFILE;
  return CONSTRUCT(ItemInfo,
    c.name = stack[0]->getShortName(factory, nullptr, stack.size() > 1);
    c.fullName = stack[0]->getNameAndModifiers(factory, false);
    c.description = stack[0]->getDescription(factory);
    c.number = stack.size();
    if (stack[0]->canEquip())
      c.slot = stack[0]->getEquipmentSlot();
    c.viewId = stack[0]->getViewObject().getViewIdList();
    c.ids.reserve(stack.size());
    for (auto it : stack)
      c.ids.push_back(it->getUniqueId());
    c.actions = {ItemAction::DROP};
    c.equiped = equiped;
    c.locked = locked;
    if (type)
      c.type = *type;
    c.pending = pending;
  );
}

static ViewId getSlotViewId(EquipmentSlot slot) {
  switch (slot) {
    case EquipmentSlot::BOOTS: return ViewId("leather_boots");
    case EquipmentSlot::WEAPON: return ViewId("sword");
    case EquipmentSlot::RINGS: return ViewId("ring");
    case EquipmentSlot::HELMET: return ViewId("leather_helm");
    case EquipmentSlot::RANGED_WEAPON: return ViewId("bow");
    case EquipmentSlot::GLOVES: return ViewId("leather_gloves");
    case EquipmentSlot::BODY_ARMOR: return ViewId("leather_armor");
    case EquipmentSlot::AMULET: return ViewId("amulet1");
    case EquipmentSlot::SHIELD: return ViewId("wooden_shield");
    case EquipmentSlot::PRAYER_BOOK: return ViewId("book");
    case EquipmentSlot::DEVOTIONAL_ITEMS: return ViewId("devotional_medal");
  }
}

static ItemInfo getEmptySlotItem(EquipmentSlot slot, bool locked) {
  return CONSTRUCT(ItemInfo,
    c.name = ""_s;
    c.fullName = ""_s;
    c.slot = slot;
    c.number = 1;
    c.viewId = {getSlotViewId(slot)};
    c.actions = {ItemAction::REPLACE};
    c.locked = locked;
    c.equiped = false;
    c.pending = false;);
}

static CollectiveInfo::PromotionOption getPromotionOption(const ContentFactory* factory, const PromotionInfo& info) {
  return {info.viewId, info.name, info.applied.getDescription(factory)};
}

void PlayerControl::fillPromotions(Creature* creature, CollectiveInfo& info) const {
  CollectiveInfo::MinionPromotionInfo promotionInfo;
  promotionInfo.viewId = creature->getViewObject().getViewIdList();
  promotionInfo.name = creature->getName().firstOrBare();
  promotionInfo.id = creature->getUniqueId();
  promotionInfo.canAdvance = creature->getPromotions().size() < creature->getAttributes().maxPromotions &&
      collective->getDungeonLevel().numPromotionsAvailable() > 0;
  for (auto& promotion : creature->getPromotions())
    promotionInfo.promotions.push_back(getPromotionOption(getGame()->getContentFactory(), promotion));
  auto contentFactory = getGame()->getContentFactory();
    for (auto& promotion : contentFactory->promotions.at(*creature->getAttributes().promotionGroup))
        promotionInfo.options.push_back(getPromotionOption(contentFactory, promotion));
    info.minionPromotions.push_back(std::move(promotionInfo));
  info.availablePromotions = int(0.001
      + double(collective->getDungeonLevel().numPromotionsAvailable()) / creature->getAttributes().promotionCost);
}

static ItemInfo getEmptySteedItemInfo(const ContentFactory* factory) {
  return CONSTRUCT(ItemInfo,
    c.name = ""_s;
    c.fullName = ""_s;
    c.viewId = {ViewId("horse")};
    c.actions = {ItemAction::REPLACE_STEED};
  );
}

static ItemInfo getSteedItemInfo(const ContentFactory* factory, const Creature* rider, const Creature* steed,
    bool currentlyRiding) {
  return CONSTRUCT(ItemInfo,
    if (rider->getSteedCompanion() == steed)
      c.name = TSentence("ITEM_INFO_NAME_COMPANION_STEED", steed->getName().bare());
    else
      c.name = steed->getName().bare();
    c.fullName = steed->getName().aOrTitle();
    c.viewId = steed->getViewObject().getViewIdList();
    c.equiped = currentlyRiding;
    c.pending = !currentlyRiding;
    c.actions = {ItemAction::DROP_STEED};
    c.ids.push_back(Item::Id(123));
  );
}

void PlayerControl::fillSteedInfo(Creature* creature, PlayerInfo& info) const {
  if (creature->isAffected(LastingEffect::RIDER)) {
    auto factory = getGame()->getContentFactory();
    if (auto steed = collective->getSteedOrRider(creature))
      info.inventory.push_back(getSteedItemInfo(factory, creature, steed, creature->getSteed() == steed));
    else
      info.inventory.push_back(getEmptySteedItemInfo(factory));
  }
}

void PlayerControl::fillEquipment(Creature* creature, PlayerInfo& info) const {
  if (!collective->usesEquipment(creature))
    return;
  auto factory = getGame()->getContentFactory();
  vector<EquipmentSlot> slots;
  for (auto slot : Equipment::slotTitles)
    slots.push_back(slot.first);
  vector<Item*> ownedItems = collective->getMinionEquipment().getItemsOwnedBy(creature);
  for (auto slot : slots) {
    vector<Item*> items;
    for (Item* it : ownedItems)
      if (it->canEquip() && it->getEquipmentSlot() == slot)
        items.push_back(it);
    for (int i = creature->getEquipment().getMaxItems(slot, creature); i < items.size(); ++i)
      // a rare occurence that minion owns too many items of the same slot,
      //should happen only when an item leaves the fortress and then is braught back
      if (!collective->getMinionEquipment().isLocked(creature, items[i]->getUniqueId()))
        collective->getMinionEquipment().discard(items[i]);
    for (Item* item : items) {
      ownedItems.removeElement(item);
      bool equiped = creature->getEquipment().isEquipped(item);
      bool locked = collective->getMinionEquipment().isLocked(creature, item->getUniqueId());
      info.inventory.push_back(getItemInfo(factory, {item}, equiped, !equiped, locked, ItemInfo::EQUIPMENT));
    }
    for (int i : Range(creature->getEquipment().getMaxItems(slot, creature) - items.size()))
      info.inventory.push_back(getEmptySlotItem(slot, collective->getMinionEquipment().isLocked(creature, slot)));
    if (slot == EquipmentSlot::WEAPON && tutorial &&
        tutorial->getHighlights(getGame()).contains(TutorialHighlight::EQUIPMENT_SLOT_WEAPON))
      info.inventory.back().tutorialHighlight = true;
  }
  vector<vector<Item*>> consumables = Item::stackItems(factory, ownedItems,
      [&](const Item* it) { if (!creature->getEquipment().hasItem(it)) return " (pending)"; else return ""; } );
  for (auto& stack : consumables)
    info.inventory.push_back(getItemInfo(factory, stack, false,
          !creature->getEquipment().hasItem(stack[0]), false, ItemInfo::CONSUMABLE));
  vector<Item*> otherItems;
  for (auto item : creature->getEquipment().getItems())
    if (!collective->getMinionEquipment().isItemUseful(item))
      otherItems.push_back(item);
  for (auto item : Item::stackItems(factory, otherItems))
    info.inventory.push_back(getItemInfo(factory, {item}, false, false, false, ItemInfo::OTHER));
  auto lockedSet = getReferenceMaybe(collective->lockedEquipmentGroups, info.groupName);
  for (auto& group : factory->equipmentGroups) {
    info.equipmentGroups.push_back(PlayerInfo::EquipmentGroupInfo {
      group.second,
      group.first,
      lockedSet && lockedSet->count(group.first)
    });
  }
}

static ScriptedUIDataElems::Record getItemRecord(const ContentFactory* factory, const vector<Item*> itemStack) {
  auto elem = ScriptedUIDataElems::Record{};
  elem.elems["view_id"] = itemStack[0]->getViewObject().getViewIdList();
  TString label = itemStack[0]->getShortName(factory, nullptr, itemStack.size() > 1);
  if (itemStack.size() > 1)
    label = TSentence("MULTIPLE_ITEMS", TString(itemStack.size()), std::move(label));
  elem.elems["name"] = capitalFirst(std::move(label));
  auto tooltipElems = ScriptedUIDataElems::List {
    capitalFirst(itemStack[0]->getNameAndModifiers(factory))
  };
  for (auto& elem : itemStack[0]->getDescription(factory))
    tooltipElems.push_back(elem);
  elem.elems["tooltip"] = std::move(tooltipElems);
  return elem;
}

static ScriptedUIDataElems::Record getItemOwnerRecord(const ContentFactory* factory, const Creature* creature,
    int count) {
  auto ret = getCreatureRecord(factory, creature);
  ret.elems["count"] = TString(toString(count));
  return ret;
}

Item* PlayerControl::chooseEquipmentItem(Creature* creature, vector<Item*> currentItems, ItemPredicate predicate,
    ScriptedUIState* uiState) {
  auto allItems = collective->getAllItems().filter(predicate);
  vector<Item*> availableItems;
  vector<Item*> usedItems;
  collective->getMinionEquipment().sortByEquipmentValue(creature, allItems);
  for (Item* item : allItems)
    if (!currentItems.contains(item)) {
      auto owner = collective->getMinionEquipment().getOwner(item);
      if (owner && getCreature(*owner))
        usedItems.push_back(item);
      else
        availableItems.push_back(item);
    }
  if (currentItems.empty() && availableItems.empty() && usedItems.empty())
    return nullptr;
  auto factory = getGame()->getContentFactory();
  vector<vector<Item*>> usedStacks = Item::stackItems(factory, usedItems,
      [&](const Item* it) {
        const Creature* c = getCreature(*collective->getMinionEquipment().getOwner(it));
        return c->getName().bare().data() + toString<int>(c->getBestAttack(factory).value);});
  auto data = ScriptedUIDataElems::Record{};
  data.elems["title"] = TString(TStringId("AVAILABLE_ITEMS_TITLE"));
  data.elems["is_equipment_choice"] = TString("blabla"_s);
  auto itemsList = ScriptedUIDataElems::List{};
  Item* ret = nullptr;
  for (Item* it : currentItems) {
    auto r = getItemRecord(factory, {it});
    r.elems["equiped"] = TString("blabla"_s);
    r.elems["callback"] = ScriptedUIDataElems::Callback {
        [it, &ret] { ret = it; return true; }
    };
    itemsList.push_back(std::move(r));
  }
  for (auto& stack : concat(Item::stackItems(factory, availableItems), usedStacks)) {
    auto r = getItemRecord(factory, stack);
    r.elems["callback"] = ScriptedUIDataElems::Callback {
        [item = stack[0], &ret] { ret = item; return true; }
    };
    unordered_set<const Creature*> allOwners;
    for (auto item : stack)
      if (auto creatureId = collective->getMinionEquipment().getOwner(item))
        if (const Creature* c = getCreature(*creatureId))
          allOwners.insert(c);
    if (!allOwners.empty())
      r.elems["owner"] = getItemOwnerRecord(factory, *allOwners.begin(), allOwners.size());
    itemsList.push_back(std::move(r));
  }
  data.elems["elems"] = std::move(itemsList);
  ScriptedUIState state;
  getView()->scriptedUI("pillage_menu", data, uiState ? *uiState : state);
  return ret;
}

static ScriptedUIDataElems::Record getSteedItemRecord(const ContentFactory* factory, const Creature* steed,
    const Creature* rider) {
  auto elem = ScriptedUIDataElems::Record{};
  elem.elems["view_id"] = steed->getViewObject().getViewIdList();
  if (rider->getSteedCompanion() == steed)
    elem.elems["name"] = TString(TSentence("ITEM_INFO_NAME_COMPANION_STEED",
        capitalFirst(steed->getName().bare())));
  else
     elem.elems["name"] = capitalFirst(steed->getName().bare());
  elem.elems["tooltip"] = ScriptedUIDataElems::List {
    capitalFirst(steed->getName().aOrTitle()),
    capitalFirst(steed->getAttributes().getDescription(factory))
  };
  return elem;
}

Creature* PlayerControl::chooseSteed(Creature* creature, vector<Creature*> allSteeds) {
  vector<Creature*> availableItems;
  vector<Creature*> usedItems;
  if (auto c = creature->getSteedCompanion())
    allSteeds.insert(0, c);
  for (auto item : allSteeds) {
    if (auto owner = collective->getSteedOrRider(item))
      usedItems.push_back(item);
    else
      availableItems.push_back(item);
  }
  if (availableItems.empty() && usedItems.empty())
    return nullptr;
  auto factory = getGame()->getContentFactory();
  vector<vector<Creature*>> usedStacks = Creature::stack(usedItems,
      [&](Creature* it) {
        const Creature* c = collective->getSteedOrRider(it);
        return c->getName().bare().data() + toString<int>(c->getBestAttack(factory).value);
      });
  auto data = ScriptedUIDataElems::Record{};
  data.elems["title"] = TString(TSentence("AVAILABLE_STEEDS", combineWithAnd(creature->getSteedSizes())));
  data.elems["is_equipment_choice"] = TString("blabla"_s);
  auto itemsList = ScriptedUIDataElems::List{};
  Creature* ret = nullptr;
  for (auto& stack : concat(Creature::stack(availableItems), usedStacks)) {
    auto r = getSteedItemRecord(getGame()->getContentFactory(), stack[0], creature);
    if (auto c = collective->getSteedOrRider(stack[0]))
      r.elems["owner"] = getItemOwnerRecord(factory, c, stack.size());
    r.elems["callback"] = ScriptedUIDataElems::Callback {
        [creature = stack[0], &ret] { ret = creature; return true; }
    };
    itemsList.push_back(std::move(r));
  }
  data.elems["elems"] = std::move(itemsList);
  getView()->scriptedUI("pillage_menu", data);
  return ret;
}
int PlayerControl::getNumMinions() const {
  return (int) collective->getCreatures(MinionTrait::FIGHTER).size();
}

typedef CollectiveInfo::Button Button;

optional<pair<ViewId, int>> PlayerControl::getCostObj(CostInfo cost) const {
  auto& resourceInfo = collective->getResourceInfo(cost.id);
  if (cost.value > 0 && resourceInfo.viewId)
    return make_pair(*resourceInfo.viewId, (int) cost.value);
  else
    return none;
}

optional<pair<ViewId, int>> PlayerControl::getCostObj(const optional<CostInfo>& cost) const {
  if (cost)
    return getCostObj(*cost);
  else
    return none;
}

ViewId PlayerControl::getViewId(const BuildInfoTypes::BuildType& info) const {
  PROFILE;
  return info.visit<ViewId>(
      [&](const BuildInfoTypes::Furniture& elem) {
        return getGame()->getContentFactory()->furniture.getData(elem.types[0]).getViewObject()->id();
      },
      [&](const BuildInfoTypes::Dig&) {
        return ViewId("dig_icon");
      },
      [&](const BuildInfoTypes::CutTree&) {
        return ViewId("dig_icon");
      },
      [&](const BuildInfoTypes::FillPit&) {
        return ViewId("fill_pit_icon");
      },
      [&](const BuildInfoTypes::Chain& c) {
        return getViewId(c[0]);
      },
      [&](const BuildInfoTypes::ImmediateDig&) {
        return ViewId("dig_icon");
      },
      [&](ZoneId zone) {
        return ::getViewId(getHighlight(zone), true);
      },
      [&](BuildInfoTypes::ClaimTile) {
        return ViewId("keeper_floor");
      },
      [&](BuildInfoTypes::UnclaimTile) {
        return ViewId("floor");
      },
      [&](BuildInfoTypes::Dispatch) {
        return ViewId("imp");
      },
      [&](const BuildInfoTypes::DestroyLayers&) {
         return ViewId("destroy_button");
      },
      [&](BuildInfoTypes::ForbidZone) {
        return ViewId("forbid_zone");
      },
      [&](BuildInfoTypes::PlaceMinion) {
        return ViewId("special_blbn");
      },
      [&](BuildInfoTypes::PlaceItem) {
        return ViewId("potion1");
      }
  );
}

vector<Button> PlayerControl::fillButtons() const {
  PROFILE;
  vector<Button> buttons;
  HashMap<CollectiveResourceId, int> resourceCache;
  auto contentFactory = getGame()->getContentFactory();
  for (auto& button : buildInfo) {
    PROFILE_BLOCK("BUTTON")
    button.type.visit<void>(
        [&](const BuildInfoTypes::Furniture& elem) {
          PROFILE_BLOCK("Furniture")
          TString description;
          if (elem.cost.value > 0) {
            int num = 0;
            for (auto type : elem.types)
              num += collective->getConstructions().getBuiltCount(type);
            if (num > 0)
              description = "[" + toString(num) + "]";
          }
          int availableNow = [&] {
            if (!elem.cost.value)
              return 1;
            if (auto res = getValueMaybe(resourceCache, elem.cost.id))
              return *res / elem.cost.value;
            auto res = collective->numResource(elem.cost.id);
            resourceCache[elem.cost.id] = res;
            return res / elem.cost.value;
          }();
          if (!collective->getResourceInfo(elem.cost.id).viewId && availableNow)
            description = combineSentences(std::move(description),
                TSentence("BUILDINGS_COUNT_AVAILABLE", TString(availableNow)));
          buttons.push_back(Button{getViewId(button.type), capitalFirst(button.getName(contentFactory)),
              getCostObj(elem.cost),
              description,
              (elem.noCredit && !availableNow) ?
                 CollectiveInfo::Button::GRAY_CLICKABLE : CollectiveInfo::Button::ACTIVE });
          },
        [&](const auto&) {
          PROFILE;
          buttons.push_back({this->getViewId(button.type), button.getName(contentFactory), none, TString(), CollectiveInfo::Button::ACTIVE});
        }
    );
    vector<TString> unmetReqText;
    for (auto& req : button.requirements) {
      PROFILE_BLOCK("Requirement")
      if (!BuildInfo::meetsRequirement(collective, req)) {
        unmetReqText.push_back(BuildInfo::getRequirementText(req, contentFactory));
        buttons.back().state = CollectiveInfo::Button::INACTIVE;
      }
    }
    if (unmetReqText.empty())
      buttons.back().help = button.help;
    else
      buttons.back().help = combineSentences(concat({button.help}, unmetReqText));
    buttons.back().key = button.key;
    buttons.back().groupName = button.groupName;
    buttons.back().hotkeyOpensGroup = button.hotkeyOpensGroup;
    buttons.back().tutorialHighlight = button.tutorialHighlight;
    buttons.back().isBuilding = button.isBuilding;
  }
  return buttons;
}

TString PlayerControl::getTriggerLabel(const AttackTrigger& trigger) const {
  return trigger.visit<TString>(
      [&](const Timer&) {
        return TStringId("YOUR_EVIL_TRIGGER");
      },
      [&](const RoomTrigger& t) {
        auto myCount = collective->getConstructions().getBuiltCount(t.type);
        return getGame()->getContentFactory()->furniture.getData(t.type).getName(myCount);
      },
      [&](const Power&) {
        return TStringId("YOUR_POWER_TRIGGER");
      },
      [&](const FinishOff&) {
        return TStringId("FINISHING_YOU_TRIGGER");
      },
      [&](const SelfVictims&) {
        return TStringId("KILLED_MEMBERS_TRIGGER");
      },
      [&](const EnemyPopulation&) {
        return TStringId("POPULATION_TRIGGER");
      },
      [&](const Resource& r) {
        return collective->getResourceInfo(r.resource).name;
      },
      [&](const StolenItems&) {
        return TStringId("THEFT_TRIGGER");
      },
      [&](const MiningInProximity&) {
        return TStringId("BREACH_TRIGGER");
      },
      [&](const Proximity&) {
        return TStringId("PROXIMITY_TRIGGER");
      },
      [&](const NumConquered&) {
        return TStringId("PROXIMITY_TRIGGER");
      },
      [&](Immediate) {
        return TStringId("IMMEDIATE_TRIGGER");
      },
      [&](AggravatingMinions) {
        return TStringId("AGGRAVATED_TRIGGER");
      }
  );
}

VillageInfo::Village PlayerControl::getVillageInfo(const Collective* col) const {
  PROFILE;
  VillageInfo::Village info;
  if (col->getName()->shortened) {
    info.name = *col->getName()->shortened;
    info.tribeName = col->getName()->race;
  } else
    info.name = col->getName()->race;
  info.id = col->getUniqueId();
  info.viewId = col->getName()->viewId;
  info.triggers.clear();
  info.type = col->getVillainType();
  info.attacking = false;
  for (auto& attack : notifiedAttacks)
    if (attack.getAttacker() == col && attack.isOngoing())
      info.attacking = true;
  auto addTriggers = [&] {
    for (auto& trigger : col->getTriggers(collective))
//#ifdef RELEASE
      if (trigger.value > 0)
//#endif
        info.triggers.push_back({getTriggerLabel(trigger.trigger), trigger.value});
  };
  if (col->getModel() == getModel()) {
    if (!collective->isKnownVillainLocation(col) && !getGame()->getOptions()->getBoolValue(OptionId::SHOW_MAP))
      info.access = VillageInfo::Village::NO_LOCATION;
    else
      info.access = VillageInfo::Village::LOCATION;
    addTriggers();
  } else if (!getGame()->isVillainActive(col))
    info.access = VillageInfo::Village::INACTIVE;
  else {
    info.access = VillageInfo::Village::ACTIVE;
    addTriggers();
  }
  if ((info.isConquered = col->isConquered())) {
    info.triggers.clear();
    if (col->attackedByPlayer && col->getControl()->canPillage(collective))
      info.action = VillageAction::PILLAGE;
  } else if (!col->getTribe()->isEnemy(collective->getTribe())) {
    if (collective->isKnownVillainLocation(col)) {
      if (col->hasTradeItems())
        info.action = VillageAction::TRADE;
    }
  }
  return info;
}

void PlayerControl::handleTrading(Collective* ally) {
  ScrollPosition scrollPos;
  auto factory = getGame()->getContentFactory();
  ScriptedUIState state;
  while (1) {
    int budget = collective->numResource(ResourceId("GOLD"));
    struct TradeOption {
      vector<Item*> items;
      StoragePositions storage;
    };
    vector<TradeOption> options;
    for (auto& elem : Item::stackItems(factory, ally->getTradeItems()))
      options.push_back({elem, collective->getStoragePositions(elem.front()->getStorageIds())});
    if (options.empty())
      return;
    bool chosen = false;
    auto data = ScriptedUIDataElems::Record{};
    data.elems["title"] = TString(TSentence("TRADE_WITH", ally->getName()->full));
    data.elems["budget"] = TString(toString(budget));
    data.elems["is_trade_or_pillage"] = TString("blabla"_s);
    auto elems = ScriptedUIDataElems::List{};
    for (int i : All(options)) {
      auto& option = options[i];
      auto elem = getItemRecord(factory, option.items);
      int price = option.items[0]->getPrice();
      elem.elems["price"] = TString(toString(price));
      if (!option.storage.empty() && price <= budget)
        elem.elems["callback"] = ScriptedUIDataElems::Callback { [option, price, ally, &budget, &chosen, this] {
          CHECK(!option.storage.empty());
          int count = 1;
          if (option.items.size() > 10) {
            int maxCount = min(budget / price, option.items.size());
            count = getView()->getNumber(TSentence("BUY_HOW_MANY", makePlural(option.items[0]->getName())), Range(0, maxCount), maxCount).value_or(0);
          }
          for (int i : Range(count)) {
            Random.choose(option.storage.asVector()).dropItem(ally->buyItem(option.items[i]));
            budget -= price;
            collective->takeResource({ResourceId("GOLD"), price});
          }
          chosen = true;
          return true;
        }};
      elems.push_back(elem);
    }
    data.elems["elems"] = std::move(elems);
    getView()->scriptedUI("pillage_menu", data, state);
    if (!chosen)
      break;
  }
}

vector<PItem> PlayerControl::retrievePillageItems(Collective* col, vector<Item*> items) {
  vector<PItem> ret;
  EntitySet<Item> index(items);
  for (auto pos : col->getTerritory().getPillagePositions())
    if (pos.getCollective() == col || !pos.getCollective()) {
      bool update = false;
      for (auto item : copyOf(pos.getInventory().getItems()))
        if (index.contains(item)) {
          ret.push_back(pos.removeItem(item));
          update = true;
        }
      if (update)
        addToMemory(pos);
    }
  if (!ret.empty())
    getGame()->addEvent(EventInfo::ItemsPillaged{});
  return ret;
}

vector<Item*> PlayerControl::getPillagedItems(Collective* col) const {
  PROFILE;
  vector<Item*> ret;
  for (Position v : col->getTerritory().getPillagePositions())
    if (v.getCollective() == col || !v.getCollective()) {
      if (!collective->getTerritory().contains(v))
        append(ret, v.getItems().filter([this, v](auto item) {
            return !collective->getStoragePositions(item->getStorageIds()).contains(v); }));
    }
  return ret;
}

void PlayerControl::handlePillage(Collective* col) {
  ScrollPosition scrollPos;
  auto factory = getGame()->getContentFactory();
  ScriptedUIState state;
  while (1) {
    struct PillageOption {
      vector<Item*> items;
      StoragePositions storage;
    };
    vector<PillageOption> options;
    for (auto& elem : Item::stackItems(factory, getPillagedItems(col)))
      options.push_back({elem, collective->getStoragePositions(elem.front()->getStorageIds())});
    if (options.empty())
      return;
    auto data = ScriptedUIDataElems::Record{};
    bool chosen = false;
    data.elems["title"] = TString(TSentence("PILLAGE", col->getName()->full));
    data.elems["is_trade_or_pillage"] = TString("blabla"_s);
    data.elems["choose_all"] = ScriptedUIDataElems::Callback { [&] {
      for (auto& elem : options)
        if (!elem.storage.empty())
          Random.choose(elem.storage.asVector()).dropItems(retrievePillageItems(col, elem.items));
      chosen = true;
      return true;
    }};
    auto elems = ScriptedUIDataElems::List{};
    for (int i : All(options)) {
      auto& option = options[i];
      auto elem = getItemRecord(factory, option.items);
      if (!option.storage.empty())
        elem.elems["callback"] = ScriptedUIDataElems::Callback { [option, col, this, &chosen] {
          CHECK(!option.storage.empty());
          Random.choose(option.storage.asVector()).dropItems(retrievePillageItems(col, option.items));
          chosen = true;
          return true;
        }};
      elems.push_back(elem);
    }
    data.elems["elems"] = std::move(elems);
    getView()->scriptedUI("pillage_menu", data, state);
    if (!chosen)
      break;
    if (auto& name = col->getName())
      collective->addRecordedEvent(TSentence("THE_PILLAGING_OF", name->full));
  }
}

vector<Collective*> PlayerControl::getKnownVillains() const {
  auto showAll = getGame()->getOptions()->getBoolValue(OptionId::SHOW_MAP);
  return getGame()->getCollectives().filter([&](Collective* c) {
      return showAll || collective->isKnownVillain(c) || !c->getTriggers(collective).empty();});
}

ViewIdList PlayerControl::getMinionGroupViewId(Creature* c) const {
  if (collective->hasTrait(c, MinionTrait::PRISONER)) {
    return {ViewId("prisoner")};
  } else
    return c->getViewObject().getViewIdList();
}

vector<Creature*> PlayerControl::getMinionGroup(const TString& groupName) const {
  return getCreatures().filter([&](auto c) {
    return collective->getMinionGroupName(c) == groupName || collective->getAutomatonGroupNames(c).contains(groupName);
  });
}

void PlayerControl::sortMinionsForUI(vector<Creature*>& minions) const {
  PROFILE;
  struct Elem {
    Creature* c;
    int value;
  };
  auto values = minions.transform([f = getGame()->getContentFactory()](Creature* c) {
    int value = 0;
    for (auto& attr : f->attrInfo)
      if (attr.second.isAttackAttr)
        value = max(value, c->getAttr(attr.first));
    return Elem{c, value};
  });
  std::sort(values.begin(), values.end(),
      [] (const Elem& v1, const Elem& v2) {
        return v1.value > v2.value || (v1.value == v2.value && v1.c->getUniqueId() > v2.c->getUniqueId());
      });
}

vector<PlayerInfo> PlayerControl::getPlayerInfos(vector<Creature*> creatures) const {
  PROFILE;
  sortMinionsForUI(creatures);
  vector<PlayerInfo> minions;
  for (Creature* c : creatures) {
    minions.emplace_back(c, getGame()->getContentFactory());
    auto& minionInfo = minions.back();
    minionInfo.groupName = chosenCreature->group;
    // only fill equipment for the chosen minion to avoid lag
    if (c->getUniqueId() == chosenCreature->id) {
      for (auto& elem : minionInfo.experienceInfo.training)
        if (auto requiredDummy = collective->getMissingTrainingFurniture(c, elem.attr))
          elem.warning = TString(TSentence("REQUIRES_TO_TRAIN_FURTHER",
              getGame()->getContentFactory()->furniture.getData(*requiredDummy).getName()));
      for (MinionActivity t : ENUM_ALL(MinionActivity))
        if (c->getAttributes().getMinionActivities().isAvailable(collective, c, t, true)) {
          minionInfo.minionTasks.push_back({t,
              !collective->isActivityGood(c, t, true),
              collective->getCurrentActivity(c).activity == t,
              c->getAttributes().getMinionActivities().isLocked(collective, c, t),
              MinionActivityMap::canLock(t),
              collective->isActivityGroupLocked(minionInfo.groupName, t)});
        }
      minionInfo.inventory.clear();
      fillSteedInfo(c, minionInfo);
      fillEquipment(c, minionInfo);
      if (canControlSingle(c) && !c->getRider())
        minionInfo.actions.push_back(PlayerInfo::Action::CONTROL);

      bool isPrisoner = collective->hasTrait(c, MinionTrait::PRISONER);
      if (!isPrisoner) {
        minionInfo.actions.push_back(PlayerInfo::Action::RENAME);
      } else
        minionInfo.experienceInfo.training.clear();
      auto& leaders = collective->getLeaders();
      if (c->isAutomaton()) {
        minionInfo.actions.push_back(PlayerInfo::Action::DISASSEMBLE);
        if (c->isAffected(LastingEffect::IMMOBILE))
          minionInfo.actions.push_back(PlayerInfo::Action::LOCK_POSITION);
      }
      else if (leaders.size() > 1 || !collective->hasTrait(c, MinionTrait::LEADER))
        minionInfo.actions.push_back(PlayerInfo::Action::BANISH);
      if (c->isAffected(BuffId("CONSUMPTION_SKILL")))
        minionInfo.actions.push_back(PlayerInfo::Action::CONSUME);
      minionInfo.actions.push_back(PlayerInfo::Action::LOCATE);
      if (collective->usesEquipment(c))
        minionInfo.actions.push_back(PlayerInfo::Action::ASSIGN_EQUIPMENT);
      if (!isPrisoner && collective->minionCanUseQuarters(c)) {
        minionInfo.actions.push_back(PlayerInfo::Action::QUARTERS);
      }
    }
  }
  return minions;
}

vector<CollectiveInfo::CreatureGroup> PlayerControl::getCreatureGroups(const vector<Creature*>& v) const {
  map<TString, CollectiveInfo::CreatureGroup> groups;
  for (Creature* c : v) {
    auto groupName = collective->getMinionGroupName(c);
    auto viewId = getMinionGroupViewId(c);
    if (!groups.count(groupName))
      groups[groupName] = { c->getUniqueId(), groupName, viewId, 0, false};
    ++groups[groupName].count;
    if (chosenCreature && chosenCreature->group == groupName)
      groups[groupName].highlight = true;
  }
  return getValues(groups);
}

vector<CollectiveInfo::CreatureGroup> PlayerControl::getAutomatonGroups(const vector<Creature*>& v) const {
  map<TString, CollectiveInfo::CreatureGroup> groups;
  for (Creature* c : v)
    for (auto& groupName : collective->getAutomatonGroupNames(c)) {
      auto viewId = getMinionGroupViewId(c);
      if (!groups.count(groupName))
        groups[groupName] = { c->getUniqueId(), groupName, viewId, 0, false};
      ++groups[groupName].count;
      if (chosenCreature && chosenCreature->group == groupName)
        groups[groupName].highlight = true;
    }
  return getValues(groups);
}

void PlayerControl::fillMinions(CollectiveInfo& info) const {
  PROFILE;
  vector<Creature*> minions;
  for (auto trait : {MinionTrait::FIGHTER, MinionTrait::PRISONER, MinionTrait::WORKER, MinionTrait::INCREASE_POPULATION, MinionTrait::LEADER})
    for (Creature* c : collective->getCreatures(trait))
      if (!minions.contains(c))
        minions.push_back(c);
  for (Creature* c : collective->getCreatures())
    if (c->isAffected(LastingEffect::STEED) && !minions.contains(c))
      minions.push_back(c);
  info.minionGroups = getCreatureGroups(minions);
  info.automatonGroups = getAutomatonGroups(minions);
  info.minions = minions.transform([](const Creature* c) { return CreatureInfo(c) ;});
  info.minionCount = collective->getPopulationSize();
  info.minionLimit = collective->getMaxPopulation();
  info.populationString = collective->getConfig().getPopulationString();
}

ItemInfo PlayerControl::getWorkshopItem(const WorkshopItem& option, int queuedCount) const {
  return CONSTRUCT(ItemInfo,
      c.number = queuedCount;
      c.name = c.number == 1 ? option.name : TSentence("ITEM_COUNT", option.pluralName, TString(c.number));
      c.viewId = option.viewId;
      c.price = getCostObj(option.cost * queuedCount);
      if (option.techId && !collective->getTechnology().researched.count(*option.techId)) {
        c.unavailable = true;
        // Work around the fact that some workshop items may require technology that the player doesn't have,
        // only happens in pre-translation saved games.
        c.unavailableReason = TSentence("MISSING_TECHNOLOGY", collective->getTechnology().techs.count(*option.techId)
            ? collective->getTechnology().getName(*option.techId)
            : TString(string(option.techId->data())));
      }
      c.description = option.description;
      c.tutorialHighlight = tutorial && option.tutorialHighlight &&
          tutorial->getHighlights(getGame()).contains(*option.tutorialHighlight);
    );
}

void PlayerControl::acquireTech(TechId tech) {
  auto techs = collective->getTechnology().getNextTechs();
  if (techs.contains(tech)) {
    if (collective->getDungeonLevel().numResearchAvailable() > 0) {
      collective->acquireTech(tech, true);
    }
  }
}

void PlayerControl::fillTechUnlocks(CollectiveInfo::LibraryInfo::TechInfo& techInfo) const {
  auto tech = techInfo.id;
  auto technology = collective->getTechnology();
  for (auto& t2 : technology.getAllowed(tech))
    techInfo.unlocks.push_back({{ViewId("book")}, technology.getName(t2),
        TStringId("TECHNOLOGY_LABEL")});
  auto contentFactory = getGame()->getContentFactory();
  for (auto& elem : buildInfo)
    for (auto& r : elem.requirements)
      if (r == tech)
        techInfo.unlocks.push_back({{getViewId(elem.type)}, elem.getName(contentFactory), TStringId("CONSTRUCTIONS_LABEL")});
  for (auto& workshop : collective->getWorkshops().types)
    for (auto& option : workshop.second.getOptions())
      if (!option.hideFromTech && option.techId == tech)
        techInfo.unlocks.push_back({{option.viewId}, option.name, TStringId("CRAFTED_ITEMS_LABEL")});
  auto& creatureFactory = getGame()->getContentFactory()->getCreatures();
  for (auto& immigrant : collective->getImmigration().getImmigrants())
    for (auto& req : immigrant.requirements)
      if (req.type.getValueMaybe<TechId>() == tech) {
        techInfo.unlocks.push_back({creatureFactory.getViewId(immigrant.getId(0)),
            creatureFactory.getName(immigrant.getId(0)), TStringId("IMMIGRANTS_LABEL")});
      }
}

void PlayerControl::fillLibraryInfo(CollectiveInfo& collectiveInfo) const {
  auto& info = collectiveInfo.libraryInfo;
  auto& dungeonLevel = collective->getDungeonLevel();
  if (dungeonLevel.numResearchAvailable() == 0)
    info.warning = TString(TStringId("CONQUER_SOME_VILLAINS"));
  info.totalProgress = 100 * dungeonLevel.getNecessaryProgress(dungeonLevel.level);
  info.currentProgress = int(100 * dungeonLevel.progress * dungeonLevel.getNecessaryProgress(dungeonLevel.level));
  auto& technology = collective->getTechnology();
  auto techs = technology.getNextTechs();
  for (auto& tech : techs) {
    info.available.emplace_back();
    auto& techInfo = info.available.back();
    techInfo.id = tech;
    techInfo.name = technology.getName(tech);
    techInfo.description = technology.techs.at(tech).description;
    techInfo.active = !info.warning && dungeonLevel.numResearchAvailable() > 0;
    fillTechUnlocks(techInfo);
  }
  for (auto& tech : collective->getTechnology().researched) {
    info.researched.emplace_back();
    auto& techInfo = info.researched.back();
    techInfo.description = technology.techs.at(tech).description;
    techInfo.name = technology.getName(tech);
    techInfo.id = tech;
    fillTechUnlocks(techInfo);
  }
}

vector<pair<Item*, Position>> PlayerControl::getItemUpgradesFor(const WorkshopItem& workshopItem) const {
  vector<pair<Item*, Position>> ret;
  if (!workshopItem.upgradeType.empty())
    for (auto& pos : collective->getConstructions().getAllStoragePositions())
      for (auto& item : pos.getItems(ItemIndex::RUNE))
        if (auto& upgradeInfo = item->getUpgradeInfo())
          if (workshopItem.upgradeType.contains(upgradeInfo->type))
            ret.push_back({make_pair(item, pos)});
  return ret;
}

struct WorkshopOptionInfo {
  ItemInfo itemInfo;
  int optionIndex;
  optional<pair<Item*, Position>> ingredient;
  optional<ImmigrantCreatureInfo> creatureInfo;
  pair<TString, int> maxUpgrades;
};

static optional<ImmigrantCreatureInfo> getImmigrantCreatureInfo(ContentFactory* factory, const ItemType& type) {
  static map<CreatureId, PCreature> creatureStats;
  auto getStats = [&](CreatureId id) -> Creature* {
    if (!creatureStats[id]) {
      creatureStats[id] = factory->getCreatures().fromId(id, TribeId::getDarkKeeper());
    }
    return creatureStats[id].get();
  };
  if (auto info = type.type->getReferenceMaybe<ItemTypes::Assembled>()) {
    auto c = getStats(info->creature);
    return getImmigrantCreatureInfo(c, factory);
  }
  return none;
}

static vector<CollectiveResourceId> getResourceTabs(const Workshops::Type& workshop) {
  vector<CollectiveResourceId> ret;
  for (auto& o : workshop.getOptions())
    if (o.materialTab && !ret.contains(o.cost.id))
      ret.push_back(o.cost.id);
  return ret;
}

struct FurnaceOptionInfo {
  ItemInfo itemInfo;
  int optionIndex;
  Item* item;
  Position pos;
};

vector<FurnaceOptionInfo> PlayerControl::getFurnaceOptions() const {
  PROFILE;
  vector<FurnaceOptionInfo> ret;
  int i = 0;
  for (auto pos : collective->getConstructions().getAllStoragePositions())
    for (auto item : pos.getItems())
      if (item->getCraftingCost().value > 0) {
        auto itemInfo = getItemInfo(getGame()->getContentFactory(), {item}, false, false, false);
        itemInfo.price = getCostObj(collective->getFurnace().getRecycledAmount(item));
        ret.push_back({itemInfo, i, item, pos});
        ++i;
      }
  return ret;
}

vector<CollectiveInfo::QueuedItemInfo> PlayerControl::getFurnaceQueue() const {
  vector<CollectiveInfo::QueuedItemInfo> ret;
  int i = 0;
  for (auto& item : collective->getFurnace().getQueued()) {
        auto itemInfo = getItemInfo(getGame()->getContentFactory(), {item.item.get()}, false, false, false);
        itemInfo.price = getCostObj(collective->getFurnace().getRecycledAmount(item.item.get()));
    ret.push_back(CollectiveInfo::QueuedItemInfo{item.state, true, std::move(itemInfo), none, {}, {TString(), 0}, i});
    ++i;
  }
  return ret;
}

vector<WorkshopOptionInfo> PlayerControl::getWorkshopOptions(int resourceIndex) const {
  vector<WorkshopOptionInfo> ret;
  auto& workshop = collective->getWorkshops().types.at(*chosenWorkshop);
  optional<CollectiveResourceId> chosenResource;
  auto resourceTabs = getResourceTabs(workshop);
  if (!resourceTabs.empty())
    chosenResource = resourceTabs[min(resourceIndex, resourceTabs.size() - 1)];
  auto& options = workshop.getOptions();
  for (int i : All(options))
    if (!options[i].materialTab || options[i].cost.id == chosenResource) {
      auto& option = options[i];
      auto itemInfo = getWorkshopItem(option, 1);
      if (option.requireIngredient) {
        for (auto& pos : collective->getConstructions().getAllStoragePositions())
          for (auto& item : pos.getItems(ItemIndex::RUNE))
            if (item->getIngredientType() == option.requireIngredient) {
              auto it = itemInfo;
              it.ingredient = getItemInfo(getGame()->getContentFactory(), {item}, false, false, false);
              it.description.push_back(TSentence("CRAFTED_FROM", item->getAName()));
              ret.push_back({it, i, make_pair(item, pos), none,
                  make_pair(!option.upgradeType.empty() ? getItemTypeName(option.upgradeType[0]) : TString(), option.maxUpgrades)});
            }
        }
      else
        ret.push_back({itemInfo, i, none, getImmigrantCreatureInfo(getGame()->getContentFactory(), option.type),
            make_pair(!option.upgradeType.empty() ? getItemTypeName(option.upgradeType[0]) : TString(), option.maxUpgrades)});
    }
  return ret;
}

CollectiveInfo::QueuedItemInfo PlayerControl::getQueuedItemInfo(const WorkshopQueuedItem& item, int cnt,
    int itemIndex) const {
  PROFILE;
  auto contentFactory = getGame()->getContentFactory();
  CollectiveInfo::QueuedItemInfo ret {item.state,
        item.paid && (!item.item.requiresUpgrades || !item.runes.empty()),
        getWorkshopItem(item.item, cnt), getImmigrantCreatureInfo(contentFactory, item.item.type),
        {}, {TString(), 0}, itemIndex};
  if (!item.paid)
    ret.itemInfo.description.push_back(TStringId("CANNOT_AFFORD_ITEM"));
  auto addItem = [this, &ret] (const ItemUpgradeInfo& info, const Item* it, bool used) {
    auto viewId = it->getViewObject().id();
    auto name = it->getName();
    for (auto& elem : ret.upgrades)
      if (elem.name == name && elem.viewId == viewId) {
        ++(used ? elem.used : elem.count);
        return;
      }
    ret.upgrades.push_back(CollectiveInfo::QueuedItemInfo::UpgradeInfo {
      viewId, name, used ? 1 : 0, used ? 0 : 1,
      it->getUpgradeInfo()->getDescription(getGame()->getContentFactory())
    });
  };
  for (auto& it : getItemUpgradesFor(item.item)) {
    addItem(*it.first->getUpgradeInfo(), it.first, false);
  }
  for (auto& it : item.runes) {
    if (auto& upgradeInfo = it->getUpgradeInfo())
      addItem(*upgradeInfo, it.get(), true);
    else {
      ret.itemInfo.ingredient = getItemInfo(getGame()->getContentFactory(), {it.get()}, false, false, false);
      ret.itemInfo.description.push_back(TSentence("CRAFTED_FROM", it->getAName()));
    }
  }
  if (item.runes.empty() && item.item.requiresUpgrades)
    ret.itemInfo.unavailableReason = TStringId("ITEM_REQUIRES_UPGRADES");
  ret.itemInfo.actions = {ItemAction::REMOVE};
  ret.maxUpgrades = !item.item.upgradeType.empty() ? make_pair(getItemTypeName(item.item.upgradeType[0]), item.item.maxUpgrades)
      : make_pair(TString(), 0);
  return ret;
}

static bool runesEqual(const vector<PItem>& v1, const vector<PItem>& v2) {
  if (v1.size() != v2.size())
    return false;
  HashSet<pair<TString, ViewId>> elems;
  for (auto& elem : v1)
    elems.insert(make_pair(elem->getName(), elem->getViewObject().id()));
  for (auto& elem : v2)
    elems.erase(make_pair(elem->getName(), elem->getViewObject().id()));
  return elems.empty();
}

vector<CollectiveInfo::QueuedItemInfo> PlayerControl::getQueuedWorkshopItems() const {
  PROFILE;
  vector<CollectiveInfo::QueuedItemInfo> ret;
  auto& queued = collective->getWorkshops().types.at(*chosenWorkshop).getQueued();
  for (int i : All(queued)) {
    if (i > 0 && queued[i - 1].indexInWorkshop == queued[i].indexInWorkshop && queued[i - 1].paid == queued[i].paid &&
        runesEqual(queued[i].runes, queued[i - 1].runes))
      ret.back() = getQueuedItemInfo(queued[ret.back().itemIndex],
          ret.back().itemInfo.number + 1, ret.back().itemIndex);
    else
      ret.push_back(getQueuedItemInfo(queued[i], 1, i));
  }
  return ret;
}

void PlayerControl::fillWorkshopInfo(CollectiveInfo& info) const {
  int index = 0;
  int i = 0;
  auto factory = getGame()->getContentFactory();
  for (auto workshopType : collective->getWorkshops().getWorkshopsTypes()) {
    auto& workshopInfo = factory->workshopInfo.at(workshopType);
    if (chosenWorkshop == workshopType)
      index = i;
    ++i;
  }
  if (chosenWorkshop == WorkshopType("FURNACE")) {
    index = i;
    info.chosenWorkshop = CollectiveInfo::ChosenWorkshopInfo {
        {},
        0,
        TString(),
        getFurnaceOptions().transform([](auto& option) {
            return CollectiveInfo::OptionInfo{option.itemInfo, none, {TString(), 0}}; }),
        getFurnaceQueue(),
        index,
        TStringId("TO_BE_SMELTED"),
        false,
        1,
        factory->attrInfo.at(AttrType("FURNACE")).viewId
    };
  } else
  if (chosenWorkshop) {
    auto& workshop = collective->getWorkshops().types.at(*chosenWorkshop);
    auto resourceTabs = getResourceTabs(workshop);
    auto resourceViewIds = resourceTabs
        .transform([&](auto id) { return collective->getResourceInfo(id).viewId.value_or(ViewId("grave")); });
    TString tabName;
    int realResourceIndex = min(resourceIndex, resourceTabs.size() - 1);
    if (!resourceTabs.empty())
      tabName = collective->getResourceInfo(resourceTabs[realResourceIndex]).name;
    auto& workshopInfo = factory->workshopInfo.at(*chosenWorkshop);
    info.chosenWorkshop = CollectiveInfo::ChosenWorkshopInfo {
        resourceViewIds,
        realResourceIndex,
        tabName,
        getWorkshopOptions(realResourceIndex).transform([](auto& option) {
            return CollectiveInfo::OptionInfo{option.itemInfo, option.creatureInfo, option.maxUpgrades}; }),
        getQueuedWorkshopItems(),
        index,
        TStringId("IN_PRODUCTION"),
        true,
        resourceTabs.empty() ? 0 : workshopInfo.getMinAttrFor(resourceTabs[realResourceIndex]),
        factory->attrInfo.at(workshopInfo.attr).viewId
    };
  }
}

void PlayerControl::acceptPrisoner(int index) {
  index = -index - 1;
  auto immigrants = getPrisonerImmigrantStack();
  if (index < immigrants.size() && !immigrants[index].collective) {
    auto victim = immigrants[index].creatures[0];
    if (victim->getBody().isHumanoid())
      victim->getAttributes().increaseBaseAttr(AttrType("DIGGING"), 15);
    collective->takePrisoner(victim);
    addMessage(PlayerMessage(TSentence("YOU_ENSLAVE", victim->getName().a())).setPosition(victim->getPosition()));
    getGame()->achieve(AchievementId("captured_prisoner"));
    for (auto& elem : copyOf(stunnedCreatures))
      if (elem.first == victim)
        stunnedCreatures.removeElement(elem);
  }
}

void PlayerControl::rejectPrisoner(int index) {
  index = -index - 1;
  auto immigrants = getPrisonerImmigrantStack();
  if (index < immigrants.size()) {
    auto victim = immigrants[index].creatures[0];
    victim->dieWithLastAttacker();
  }
}

vector<PlayerControl::StunnedInfo> PlayerControl::getPrisonerImmigrantStack() const {
  vector<StunnedInfo> ret;
  vector<Creature*> outside;
  HashMap<Collective*, vector<Creature*>> inside;
  for (auto stunned : stunnedCreatures)
    if (stunned.first->isAffected(LastingEffect::STUNNED) && !stunned.first->isDead()) {
      if (auto villain = stunned.second) {
        if (villain->isConquered() || !villain->getTerritory().contains(stunned.first->getPosition()))
          outside.push_back(stunned.first);
        else
          inside[villain].push_back(stunned.first);
      } else
        outside.push_back(stunned.first);
    }
  for (auto& elem : inside)
    for (auto& stack : Creature::stack(elem.second))
      ret.push_back(StunnedInfo{stack, elem.first});
  for (auto& stack : Creature::stack(outside))
    ret.push_back(StunnedInfo{stack, nullptr});
  return ret;
}

vector<ImmigrantDataInfo> PlayerControl::getUnrealizedPromotionsImmigrantData() const {
  PROFILE;
  vector<ImmigrantDataInfo> ret;
  auto contentFactory = getGame()->getContentFactory();
  auto getLuxuryString = [](double value) {
    auto ret = toString(round(value * 10) / 10);
    if (ret == "0")
      return TString("0.1"_s);
    return TString(ret);
  };
  for (auto c : getCreatures())
    if (c->getCombatExperience(true, false) < c->getCombatExperience(false, false)) {
      ret.emplace_back();
      ret.back().requirements.push_back(TStringId("REQUIRES_MORE_LUXURIOUS_QUARTERS"));
      auto req = getRequiredLuxury(c->getCombatExperience(false, false));
      if (req == 0)
        ret.back().requirements.push_back(TStringId("REQUIRES_PERSONAL_QUARTERS"));
      else {
        auto cur = collective->getZones().getQuartersLuxury(c->getUniqueId());
        if (!cur)
          ret.back().requirements.push_back(TSentence("REQUIRES_PERSONAL_QUARTERS_WITH_LUXURY", getLuxuryString(req)));
        else
          ret.back().requirements.push_back(TSentence("REQUIRES_MORE_LUXURY", getLuxuryString(req - *cur), getLuxuryString(req)));
      }
      ret.back().creature = getImmigrantCreatureInfo(c, contentFactory);
      ret.back().id = -123456;
    }
  return ret;
}

vector<ImmigrantDataInfo> PlayerControl::getPrisonerImmigrantData() const {
  PROFILE;
  vector<ImmigrantDataInfo> ret;
  int index = -1;
  auto contentFactory = getGame()->getContentFactory();
  const auto getPrisonSize = [this, contentFactory] (BedType prisonBedType) {
    if (!prisonSizeCache[prisonBedType]) {
      PROFILE_BLOCK("prisonSize");
      int cnt = 0;
      auto& constructions = collective->getConstructions();
      for (auto type : contentFactory->furniture.getBedFurniture(prisonBedType))
        if (type == FurnitureType("PRISON")) {
          for (auto& pos : constructions.getBuiltPositions(type))
            if (pos.isClosedOff(MovementType(MovementTrait::WALK).setPrisoner()))
              ++cnt;
        } else
          cnt += constructions.getBuiltCount(type);
      prisonSizeCache[prisonBedType] = cnt;
    }
    return *prisonSizeCache[prisonBedType];
  };
  for (auto stack : getPrisonerImmigrantStack()) {
    auto c = stack.creatures[0];
    const auto prisonBedType = CollectiveConfig::getPrisonBedType(c);
    const int numPrisoners = collective->getCreatures(MinionTrait::PRISONER)
        .filter([&](auto other) {  return CollectiveConfig::getPrisonBedType(other) == prisonBedType; })
        .size();
    vector<TString> requirements;
    const auto prisonSize = getPrisonSize(prisonBedType);
    const int missingSize = numPrisoners + 1 - prisonSize;
    if (missingSize > 0) {
      if (prisonSize == 0)
        requirements.push_back(TSentence("REQUIRES_A", getName(prisonBedType)));
      else
        requirements.push_back(TSentence("REQUIRES_X_MORE", makePlural(getName(prisonBedType)), TString(missingSize)));
    }
    if (stack.collective)
      requirements.push_back(TSentence("REQUIRES_CONQUERING", stack.collective->getName()->full));
    ret.emplace_back();
    ret.back().requirements = requirements;
    ret.back().creature = getImmigrantCreatureInfo(c, contentFactory);
    ret.back().creature.name = TSentence("PRISONER_FROM_NAME",  std::move(ret.back().creature.name));
    ret.back().count = stack.creatures.size() == 1 ? none : optional<int>(stack.creatures.size());
    ret.back().timeLeft = c->getTimeRemaining(LastingEffect::STUNNED);
    ret.back().id = index;
    --index;
  }
  return ret;
}

vector<ImmigrantDataInfo> PlayerControl::getNecromancerImmigrationHelp() const {
  vector<ImmigrantDataInfo> ret;
  ret.push_back(ImmigrantDataInfo());
  ret.back().creature = ImmigrantCreatureInfo {
    TStringId("BUILD_MORGUE_HINT"),
    {ViewId("morgue_table")},
    {},
    {},
    {}
  };
  ret.back().id = -1000;
  ret.back().info = {TStringId("BALSAMS_UPGRADE_HINT")};
  return ret;
}

static ImmigrantDataInfo::SpecialTraitInfo getSpecialTraitInfo(const SpecialTrait& trait, const ContentFactory* factory) {
  using TraitInfo = ImmigrantDataInfo::SpecialTraitInfo;
  return trait.visit<ImmigrantDataInfo::SpecialTraitInfo>(
      [&] (const ExtraTraining& t) {
        return TraitInfo{TSentence("EXTRA_TRAINING_POTENTIAL", factory->attrInfo.at(t.type).name), false};
      },
      [&] (const CompanionInfo& t) {
        return TraitInfo{TSentence("IMMIGRANT_COMPANION",
          capitalFirst(factory->getCreatures().getName(t.creatures[0]))), false};
      },
      [&] (const AttrBonus& t) {
        return TraitInfo{TSentence("IMMIGRANT_ATTR_BONUS", toStringWithSign(t.increase), factory->attrInfo.at(t.attr).name), t.increase <= 0};
      },
      [&] (const SpecialAttr& t) {
        return TraitInfo{
            TSentence("SPECIAL_ATTR_ADJECTIVE", {toStringWithSign(t.value), factory->attrInfo.at(t.attr).name,
                t.predicate.getName(factory)}),
            t.value < 0};
      },
      [&] (const Lasting& effect) {
        auto adj = getAdjective(effect.effect, factory);
        if (effect.time)
          return TraitInfo{TSentence("TEMPORARY_TRAIT", adj, TString(*effect.time)),
              isConsideredBad(effect.effect, factory)};
        else
          return TraitInfo{TSentence("PERMANENT_TRAIT", adj), isConsideredBad(effect.effect, factory)};
      },
      [&] (ExtraBodyPart part) {
        if (part.count == 1)
          return TraitInfo{TSentence("EXTRA_BODY_PART", getName(part.part)), false};
        else
          return TraitInfo{TSentence("EXTRA_BODY_PARTS", TString(part.count), makePlural(getName(part.part))), false};
      },
      [&] (const ExtraIntrinsicAttack& a) {
        return TraitInfo{capitalFirst(a.item.get(factory)->getName()), false};
      },
      [&] (const OneOfTraits&) -> TraitInfo {
        FATAL << "Can't draw traits alternative";
        fail();
      }
  );
}

void PlayerControl::fillImmigration(CollectiveInfo& info) const {
  PROFILE;
  info.immigration.clear();
  auto& immigration = collective->getImmigration();
  info.immigration.append(getUnrealizedPromotionsImmigrantData());
  info.immigration.append(getPrisonerImmigrantData());
  auto factory = getGame()->getContentFactory();
  if (collective->getWorkshops().getWorkshopsTypes().contains(WorkshopType("MORGUE")))
    info.immigration.append(getNecromancerImmigrationHelp());
  for (auto& elem : immigration.getAvailable()) {
    PROFILE_BLOCK("elem");
    const auto& candidate = elem.second.get();
    if (candidate.getInfo().isInvisible())
      continue;
    const int count = (int) candidate.getCreatures().size();
    optional<TimeInterval> timeRemaining;
    if (auto time = candidate.getEndTime())
      timeRemaining = *time - getGame()->getGlobalTime();
    vector<TString> infoLines;
    Creature* c = candidate.getCreatures()[0];
    candidate.getInfo().visitRequirements(makeVisitor(
        [&](const Pregnancy&) {
          optional<TimeInterval> maxT;
          for (Creature* c : collective->getCreatures())
            if (c->isAffected(LastingEffect::PREGNANT))
              if (auto remaining = c->getTimeRemaining(LastingEffect::PREGNANT))
                if (!maxT || *remaining > *maxT)
                  maxT = *remaining;
          if (maxT && (!timeRemaining || *maxT > *timeRemaining))
            timeRemaining = *maxT;
        },
        [&](const RecruitmentInfo& info) {
          auto recruits = info.getAvailableRecruits(collective, candidate.getInfo().getNonRandomId(0));
          infoLines.push_back(TSentence("N_RECRUITS_AVAILABLE", TString(recruits.size())));
          c = recruits[0];
        },
        [&](const auto&) {}
    ));
    if (count > 1)
      infoLines.push_back(TStringId("GROUP_TAKES_ONE_POP"));
    for (auto trait : candidate.getInfo().getTraits())
      if (auto desc = getImmigrantDescription(trait))
        infoLines.push_back(*desc);
    info.immigration.push_back(ImmigrantDataInfo());
    info.immigration.back().requirements = immigration.getMissingRequirements(candidate);
    info.immigration.back().info = infoLines;
    info.immigration.back().specialTraits = candidate.getSpecialTraits().transform(
        [&](const auto& trait){ return getSpecialTraitInfo(trait, factory); });
    info.immigration.back().cost = getCostObj(candidate.getCost());
    info.immigration.back().creature = getImmigrantCreatureInfo(c, factory);
    info.immigration.back().count = count == 1 ? none : optional<int>(count);
    info.immigration.back().timeLeft = timeRemaining;
    info.immigration.back().id = elem.first;
    info.immigration.back().generatedTime = candidate.getCreatedTime();
    info.immigration.back().keybinding = candidate.getInfo().getKeybinding();
    info.immigration.back().tutorialHighlight = candidate.getInfo().getTutorialHighlight();
  }
  sort(info.immigration.begin(), info.immigration.end(),
      [](const ImmigrantDataInfo& i1, const ImmigrantDataInfo& i2) {
        return (i1.timeLeft && (!i2.timeLeft || *i1.timeLeft > *i2.timeLeft)) ||
            (!i1.timeLeft && !i2.timeLeft && i1.id > i2.id);
      });
}

void PlayerControl::fillImmigrationHelp(CollectiveInfo& info) const {
  info.allImmigration.clear();
  auto contentFactory = getGame()->getContentFactory();
  static map<CreatureId, PCreature> creatureStats;
  auto getStats = [&](CreatureId id) -> Creature* {
    if (!creatureStats[id]) {
      creatureStats[id] = contentFactory->getCreatures().fromId(id, TribeId::getDarkKeeper());
    }
    return creatureStats[id].get();
  };
  for (auto elem : Iter(collective->getImmigration().getImmigrants())) {
    if (elem->isHiddenInHelp() || elem->isInvisible())
      continue;
    auto creatureId = elem->getNonRandomId(0);
    Creature* c = getStats(creatureId);
    optional<pair<ViewId, int>> costObj;
    vector<TString> requirements;
    vector<TString> infoLines;
    auto game = collective->getGame();
    elem->visitRequirements(makeVisitor(
        [&](const AttractionInfo& attraction) {
          int required = attraction.amountClaimed;
          if (required > 0)
            requirements.push_back(TSentence("REQUIRES_ATTRACTIONS", TString(required),
                combineWithOr(attraction.types.transform([&](const AttractionType& type) {
                  return AttractionInfo::getAttractionName(contentFactory, type, required); }))));
        },
        [&](const TechId& techId) {
          requirements.push_back(TSentence("MISSING_TECHNOLOGY", collective->getTechnology().getName(techId)));
        },
        [&](const ImmigrantFlag& f) {
          requirements.push_back(TStringId("REQUIRES_UNLOCKING"));
        },
        [&](const SunlightState& state) {
          requirements.push_back(TSentence("IMMIGRANT_WONT_JOIN_DURING", game->getSunlightInfo().getText()));
        },
        [&](const FurnitureType& type) {
          requirements.push_back(TSentence("REQUIRES_FURNITURE", contentFactory->furniture.getData(type).getName()));
        },
        [&](const CostInfo& cost) {
          costObj = getCostObj(cost);
          if (!costObj && cost.id == ResourceId("DEMON_PIETY"))
            requirements.push_back(TSentence("REQUIRES_RESOURCE", collective->getResourceInfo(cost.id).name));
        },
        [&](const ExponentialCost& cost) {
          auto& resourceInfo = collective->getResourceInfo(cost.base.id);
          if (resourceInfo.viewId)
            costObj = make_pair(*resourceInfo.viewId, (int) cost.base.value);
          infoLines.push_back(TSentence("COST_DOUBLES_FOR_EVERY", TString(cost.numToDoubleCost),
              c->getName().plural()));
          if (cost.numFree > 0)
            infoLines.push_back(TSentence("FIRST_X_ARE_FREE", TString(cost.numFree), c->getName().plural()));
        },
        [&](const Pregnancy&) {
          requirements.push_back(TStringId("REQUIRES_PREGNANT_SUCCUBUS"));
        },
        [&](const RecruitmentInfo& info) {
          if (!info.findEnemy(getGame()).empty())
            requirements.push_back(TStringId("REQUIRES_ALLY_DISCOVERED_AND_HAVE_RECRUITS"));
          else
            requirements.push_back(TStringId("ALLY_DOESNT_EXIST"));
        },
        [&](const TutorialRequirement&) {
        },
        [&](const MinTurnRequirement&) {
        },
        [&](const NegateRequirement&) {
        }
    ));
    if (auto limit = elem->getLimit())
      infoLines.push_back(TSentence("LIMITED_TO_N_CREATURES", TString(*limit)));
    for (auto trait : elem->getTraits())
      if (auto desc = getImmigrantDescription(trait))
        infoLines.push_back(*desc);
    info.allImmigration.push_back(ImmigrantDataInfo());
    info.allImmigration.back().requirements = requirements;
    info.allImmigration.back().info = infoLines;
    info.allImmigration.back().cost = costObj;
    info.allImmigration.back().creature = getImmigrantCreatureInfo(c, contentFactory);
    info.allImmigration.back().id = elem.index();
    info.allImmigration.back().autoState = collective->getImmigration().getAutoState(elem.index());
  }
  if (collective->getConfig().canCapturePrisoners()) {
    info.allImmigration.push_back(ImmigrantDataInfo());
    info.allImmigration.back().requirements = {TStringId("REQUIRES_TWO_PRISON_TILES"),
        TStringId("REQUIRES_KNOCKING_OUT_ENEMY")};
    info.allImmigration.back().info = {TStringId("CONTRIBUTES_TO_WORK_FORCE"),
        TStringId("CAN_BE_CONVERTED")};
    info.allImmigration.back().creature = ImmigrantCreatureInfo {
        TStringId("PRISONER"),
        {ViewId("prisoner")},
        {}
    };
    info.allImmigration.back().id =-1;
  }
}

static optional<CollectiveInfo::RebellionChance> getRebellionChance(double prob) {
  if (prob > 0.6)
    return CollectiveInfo::RebellionChance::HIGH;
  if (prob > 0.2)
    return CollectiveInfo::RebellionChance::MEDIUM;
  if (prob > 0)
    return CollectiveInfo::RebellionChance::LOW;
  return none;
}

void PlayerControl::fillCurrentLevelInfo(GameInfo& gameInfo) const {
  PROFILE;
  auto level = getCurrentLevel();
  auto levels = getModel()->getDungeonBranch(level, getMemory());
  gameInfo.currentLevel = CurrentLevelInfo {
    level->name,
    *levels.findElement(level),
    levels.transform([](auto level) { return level->name; }),
  };
}

void PlayerControl::fillDungeonLevel(AvatarLevelInfo& info) const {
  const auto& dungeonLevel = collective->getDungeonLevel();
  info.level = dungeonLevel.level + 1;
  if (auto leader = collective->getLeaders().getFirstElement()) {
    info.viewId = leader[0]->getViewObject().getViewIdList();
    info.title = leader[0]->getName().title();
  }
  info.progress = dungeonLevel.progress;
  info.numAvailable = min(dungeonLevel.numResearchAvailable(), collective->getTechnology().getNextTechs().size());
}

void PlayerControl::fillResources(CollectiveInfo& info) const {
  info.numResource.clear();
  for (auto& resource : getGame()->getContentFactory()->resourceOrder)
    if (usedResources.count(resource)) {
      auto& elem = getGame()->getContentFactory()->resourceInfo.at(resource);
      if (elem.viewId)
        info.numResource.push_back(
            {*elem.viewId, collective->numResourcePlusDebt(resource), elem.name, elem.tutorialHighlight});
    }
}

void PlayerControl::refreshGameInfo(GameInfo& gameInfo) const {
  PROFILE;
  auto contentFactory = getGame()->getContentFactory();
  gameInfo.scriptedHelp = contentFactory->scriptedHelp;
  gameInfo.isSingleMap = getGame()->isSingleModel();
  getGame()->getEncyclopedia()->setKeeperThings(contentFactory, &collective->getTechnology(), &collective->getWorkshops());
  gameInfo.encyclopedia = getGame()->getEncyclopedia();
  gameInfo.takingScreenshot = takingScreenshot;
  fillCurrentLevelInfo(gameInfo);
  if (tutorial)
    tutorial->refreshInfo(getGame(), gameInfo.tutorial);
  gameInfo.villageInfo.villages.clear();
  gameInfo.villageInfo.numMainVillains = gameInfo.villageInfo.numConqueredMainVillains = 0;
  for (auto& col : getGame()->getVillains(VillainType::MAIN)) {
    ++gameInfo.villageInfo.numMainVillains;
    if (col->isConquered())
      ++gameInfo.villageInfo.numConqueredMainVillains;
  }
  for (auto& col : getKnownVillains())
    if (col->getName() && col->isDiscoverable())
      gameInfo.villageInfo.villages.push_back(getVillageInfo(col));
  gameInfo.villageInfo.dismissedInfos = dismissedVillageInfos;
  std::stable_sort(gameInfo.villageInfo.villages.begin(), gameInfo.villageInfo.villages.end(),
       [](const auto& v1, const auto& v2) { return (int) v1.type < (int) v2.type; });
  SunlightInfo sunlightInfo = getGame()->getSunlightInfo();
  gameInfo.sunlightInfo = { sunlightInfo.getText(), sunlightInfo.getTimeRemaining() };
  gameInfo.infoType = GameInfo::InfoType::BAND;
  gameInfo.playerInfo = CollectiveInfo();
  auto& info = *gameInfo.playerInfo.getReferenceMaybe<CollectiveInfo>();
  info.buildings = fillButtons();
  fillMinions(info);
  fillImmigration(info);
  fillImmigrationHelp(info);
  info.chosenCreature.reset();
  if (chosenCreature)
    if (Creature* c = getCreature(chosenCreature->id)) {
      if (!getChosenTeam())
        info.chosenCreature = CollectiveInfo::ChosenCreatureInfo {
            chosenCreature->id, getPlayerInfos(getMinionGroup(chosenCreature->group)), none};
      else
        info.chosenCreature = CollectiveInfo::ChosenCreatureInfo {
            chosenCreature->id, getPlayerInfos(getTeams().getMembers(*getChosenTeam())), *getChosenTeam()};
    }
  fillWorkshopInfo(info);
  fillLibraryInfo(info);
  info.minionPromotions.clear();
  info.availablePromotions = 0;
  for (auto c : collective->getCreatures())
    if (c->getAttributes().promotionGroup)
      fillPromotions(c, info);
  fillDungeonLevel(info.avatarLevelInfo);
  fillResources(info);
  gameInfo.time = collective->getGame()->getGlobalTime();
  gameInfo.modifiedSquares = gameInfo.totalSquares = 0;
  gameInfo.modifiedSquares += getCurrentLevel()->getNumGeneratedSquares();
  gameInfo.totalSquares += getCurrentLevel()->getNumTotalSquares();
  info.teams.clear();
  for (int i : All(getTeams().getAll())) {
    TeamId team = getTeams().getAll()[i];
    info.teams.emplace_back();
    for (Creature* c : getTeams().getMembers(team))
      info.teams.back().members.push_back(c->getUniqueId());
    info.teams.back().active = getTeams().isActive(team);
    info.teams.back().id = team;
    if (getChosenTeam() == team)
      info.teams.back().highlight = true;
  }
  gameInfo.messageBuffer = messages;
  info.taskMap.clear();
  for (const Task* task : collective->getTaskMap().getAllTasks()) {
    optional<UniqueEntity<Creature>::Id> creature;
    if (auto c = collective->getTaskMap().getOwner(task))
      creature = c->getUniqueId();
    info.taskMap.push_back(CollectiveInfo::Task{task->getDescription(), creature,
        collective->getTaskMap().isPriorityTask(task)});
    if (info.taskMap.size() > 200)
      break;
  }
  const auto maxEnemyCountdown = 500_visible;
  if (auto& enemies = getModel()->getExternalEnemies())
    if (auto nextWave = enemies->getNextWave()) {
      if (!nextWave->enemy.behaviour.contains<HalloweenKids>()) {
        auto countDown = nextWave->attackTime - getModel()->getLocalTime() + (*enemies->getStartTime() - 0_local);
        auto index = enemies->getNextWaveIndex();
        auto name = nextWave->enemy.name;
        auto viewId = nextWave->viewId;
        if (index % 6 == 5) {
          name = TStringId("ENEMY_WAVE_NAME_UNKNOWN");
          viewId = {ViewId("unknown_monster")};
        }
        if (!dismissedNextWaves.count(index) && countDown < maxEnemyCountdown)
          info.nextWave = CollectiveInfo::NextWave {
            viewId,
            name,
            countDown
          };
      }
    }
  if (auto rebellionWarning = getRebellionChance(collective->getRebellionProbability()))
    if (!lastWarningDismiss || getModel()->getLocalTime() > *lastWarningDismiss + 1000_visible)
      info.rebellionChance = *rebellionWarning;
}

void PlayerControl::addMessage(const PlayerMessage& msg) {
  messages.push_back(msg);
  messageHistory.push_back(msg);
  if (msg.getPriority() == MessagePriority::CRITICAL) {
    getView()->stopClock();
    for (auto c : getControlled()) {
      c->privateMessage(msg);
      break;
    }
  }
}

void PlayerControl::updateMinionVisibility(const Creature* c) {
  PROFILE;
  auto visibleTiles = c->getVisibleTiles();
  visibilityMap->update(c, visibleTiles);
  for (auto& pos : visibleTiles) {
    if (collective->addKnownTile(pos) && c->getPosition().dist8(pos).value_or(11) <= 10)
      updateKnownLocations(pos);
    addToMemory(pos);
  }
}

void PlayerControl::addWindowMessage(ViewIdList viewId, const TString& message) {
  getView()->windowedMessage(viewId, message);
}

void PlayerControl::onEvent(const GameEvent& event) {
  using namespace EventInfo;
  event.visit<void>(
      [&](const Projectile& info) {
        if (getControlled().empty() && (canSee(info.begin) || canSee(info.end)) && info.begin.isSameLevel(getCurrentLevel())) {
          getView()->animateObject(info.begin.getCoord(), info.end.getCoord(), info.viewId, info.fx);
          if (info.sound)
            getView()->addSound(*info.sound);
        }
      },
      [&](const ConqueredEnemy& info) {
        auto col = info.collective;
        if (col == collective)
          return;
        if (col->isDiscoverable() && info.byPlayer) {
          if (auto& name = col->getName()) {
            collective->addRecordedEvent(TSentence("THE_CONQUERING_OF", name->full));
            addWindowMessage(ViewIdList{name->viewId}, TSentence("TRIBE_OF_IS_DESTROYED", name->full));
          } else
            addMessage(PlayerMessage(TStringId("UNNAMED_TRIBE_DESTROYED"), MessagePriority::CRITICAL));
          collective->getDungeonLevel().onKilledVillain(col->getVillainType());
        }
        for (auto pos : col->getTerritory().getPillagePositions())
          if (auto c = pos.getCreature())
            if (c->isAffected(LastingEffect::STUNNED))
              if (auto traits = collective->stunnedMinions.getMaybe(c)) {
                c->removeEffect(LastingEffect::STUNNED);
                battleSummary.minionsCaptured.removeElementMaybe(c);
                for (auto t : *traits)
                  collective->setTrait(c, t);
              }
        vector<Creature*> freed;
        for (auto c : copyOf(col->getCreatures()))
          if (auto traits = collective->stunnedMinions.getMaybe(c)) {
            c->getStatus().erase(CreatureStatus::PRISONER);
            freed.push_back(c);
            collective->addCreature(c, *traits);
          }
        if (!freed.empty())
          presentMinionsFreedMessage(freed);
      },
      [&](const CreatureEvent& info) {
        if (collective->getCreatures().contains(info.creature))
          addMessage(PlayerMessage(info.message).setCreature(info.creature->getUniqueId()));
      },
      [&](const VisibilityChanged& info) {
        visibilityMap->onVisibilityChanged(info.pos);
      },
      [&](const CreatureMoved& info) {
        if (getCreatures().contains(info.creature))
          updateMinionVisibility(info.creature);
      },
      [&](const ItemsOwned& info) {
        auto& equipment = collective->getMinionEquipment();
        if (getCreatures().contains(info.creature))
          for (auto item : info.items)
            if (!equipment.isOwner(item, info.creature) && !equipment.tryToOwn(info.creature, item))
              getView()->presentText(none, TSentence("ITEM_WONT_BE_ASSIGNED_BECAUSE_SLOT_LOCKED", item->getTheName()));
      },
      [&](const WonGame&) {
        getGame()->conquered(*collective->getName()->shortened, collective->getKills().getSize(),
            (int) collective->getDangerLevel() + collective->getPoints());
        getView()->presentText(none, TStringId("RETIRE_WHEN_READY"));
        getGame()->achieve(AchievementId("won_keeper"));
        auto& leaders = collective->getLeaders();
        if (leaders.size() == 1 && leaders[0]->getBody().getHealth() <= 0.05)
          getGame()->achieve(AchievementId("won_game_low_health"));
        if (soloKeeper)
          getGame()->achieve(AchievementId("solo_keeper"));
      },
      [&](const RetiredGame&) {
        if (getGame()->getVillains(VillainType::MAIN).empty())
          // No victory condition in this game, so we generate a highscore when retiring.
          getGame()->retired(*collective->getName()->shortened, collective->getKills().getSize(),
              (int) collective->getDangerLevel() + collective->getPoints());
      },
      [&](const TechbookRead& info) {
        auto tech = info.technology;
        auto& technology = collective->getTechnology();
        vector<TechId> nextTechs = technology.getNextTechs();
        if (!technology.researched.count(tech)) {
          if (!nextTechs.contains(tech))
            getView()->presentText(none, TSentence("TOME_CANT_BE_USED", technology.getName(tech)));
          else {
            getView()->presentText(none, TSentence("TOME_ACQUIRED", technology.getName(tech)));
            collective->acquireTech(tech, false);
          }
        } else {
          getView()->presentText(none, TSentence("TOME_ALREADY_KNOWN", technology.getName(tech)));
        }
      },
      [&](const CreatureStunned& info) {
        if (collective->getCreatures().contains(info.attacker))
          battleSummary.enemiesCaptured.push_back(info.victim);
        for (auto villain : getGame()->getCollectives())
          if (villain->getCreatures().contains(info.victim)) {
            if (villain != collective)
              stunnedCreatures.push_back({info.victim, villain});
            return;
          }
        stunnedCreatures.push_back({info.victim, nullptr});
      },
      [&](const CreatureKilled& info) {
        auto pos = info.victim->getPosition();
        if (canSee(pos) && pos.isSameLevel(getCurrentLevel()))
          if (auto anim = info.victim->getBody().getDeathAnimation(getGame()->getContentFactory()))
            getView()->animation(pos.getCoord(), *anim);
        if (collective->getCreatures().contains(info.attacker))
          battleSummary.enemiesKilled.push_back(info.victim);
      },
      [&](const CreatureAttacked& info) {
        auto pos = info.victim->getPosition();
        if (canSee(pos) && pos.isSameLevel(getCurrentLevel())) {
          auto dir = info.attacker->getPosition().getDir(pos);
          if (dir.length8() == 1) {
            auto orientation = dir.getCardinalDir();
            if (info.damageAttr == AttrType("DAMAGE"))
              getView()->animation(pos.getCoord(), AnimationId::ATTACK, orientation);
            else if (auto& fx = getGame()->getContentFactory()->attrInfo.at(info.damageAttr).meleeFX)
              getView()->animation(FXSpawnInfo(*fx, pos.getCoord(), Vec2(0, 0)));
          }
        }
      },
      [&](const FurnitureRemoved& info) {
        if (getGame()->getContentFactory()->furniture.getData(info.type).isEyeball())
          visibilityMap->removeEyeball(info.position);
        if (info.type == FurnitureType("PIT") && collective->getKnownTiles().isKnown(info.position))
          addToMemory(info.position);
      },
      [&](const FX& info) {
        if (getControlled().empty() && canSee(info.position) && info.position.isSameLevel(getCurrentLevel()))
          getView()->animation(FXSpawnInfo(info.fx, info.position.getCoord(), info.direction.value_or(Vec2(0, 0))));
      },
      [&](const LeaderWounded& info) {
        leaderWoundedTime.set(info.c, getModel()->getLocalTime());
      },
      [&](const auto&) {}
  );
}

void PlayerControl::updateKnownLocations(const Position& pos) {
  PROFILE;
  /*if (pos.getModel() == getModel())
    if (const Location* loc = pos.getLocation())
      if (!knownLocations.count(loc)) {
        knownLocations.insert(loc);
        if (auto name = loc->getName())
          addMessage(PlayerMessage("Your minions discover the location of " + *name, MessagePriority::HIGH)
              .setLocation(loc));
        else if (loc->isMarkedAsSurprise())
          addMessage(PlayerMessage("Your minions discover a new location.").setLocation(loc));
      }*/
  if (auto game = getGame()) // check in case this method is called before Game is constructed
    for (Collective* col : game->getCollectives())
      if (col != collective && col->getTerritory().contains(pos)) {
        collective->addKnownVillain(col);
        if (!collective->isKnownVillainLocation(col)) {
          if (auto& a = col->getConfig().discoverAchievement)
            game->achieve(*a);
          collective->addKnownVillainLocation(col);
          if (col->isDiscoverable())
            if (auto& name = col->getName())
              addMessage(PlayerMessage(TSentence("MINIONS_DISCOVER_LOCATION", name->full),
                  MessagePriority::HIGH).setPosition(pos));
        }
      }
}


const MapMemory& PlayerControl::getMemory() const {
  return *memory;
}

void PlayerControl::getSquareViewIndex(Position pos, bool canSee, ViewIndex& index) const {
  // use the leader as a generic viewer
  auto leaders = collective->getLeaders();
  if (leaders.empty()) { // if no leader try any creature, else bail out
    auto& creatures = collective->getCreatures();
    if (!creatures.empty())
      leaders = {creatures[0]};
    else
      return;
  }
  if (canSee)
    pos.getViewIndex(index, leaders[0]);
  else
    index.setHiddenId(pos.getTopViewId());
  if (const Creature* c = pos.getCreature())
    if (canSee) {
      index.modEquipmentCounts() = c->getEquipment().getCounts();
      index.insert(c->getViewObject());
      if (auto steed = c->getSteed()) {
        index.getObject(ViewLayer::CREATURE).setModifier(ViewObjectModifier::RIDER);
        auto obj = steed->getViewObject();
        obj.setLayer(ViewLayer::STEED);
        obj.getCreatureStatus().intersectWith(getDisplayedOnMinions());
        index.insert(std::move(obj));
      }
      auto& object = index.getObject(ViewLayer::CREATURE);
      if (isEnemy(c)) {
        object.setModifier(ViewObject::Modifier::HOSTILE);
        if (c->canBeCaptured() && collective->getConfig().canCapturePrisoners())
          object.setClickAction(c->isCaptureOrdered() ?
              ViewObjectAction::CANCEL_CAPTURE_ORDER : ViewObjectAction::ORDER_CAPTURE);
      } else
        object.getCreatureStatus().intersectWith(getDisplayedOnMinions());
    }
}

void PlayerControl::getViewIndex(Vec2 pos, ViewIndex& index) const {
  PROFILE;
  auto furnitureFactory = &getGame()->getContentFactory()->furniture;
  Position position(pos, getCurrentLevel());
  if (!position.isValid())
    return;
  if (auto belowPos = position.getGroundBelow()) {
    if (auto memIndex = getMemory().getViewIndex(belowPos->first))
      index.mergeGroundBelow(*memIndex, belowPos->second);
    return;
  }
  bool canSeePos = canSee(position);
  getSquareViewIndex(position, canSeePos, index);
  if (!canSeePos)
    if (auto memIndex = getMemory().getViewIndex(position))
      index.mergeFromMemory(*memIndex);
  if (draggedCreature)
    if (Creature* c = getCreature(*draggedCreature))
      for (auto task : collective->getTaskMap().getTasks(position))
        if (auto activity = collective->getTaskMap().getTaskActivity(task))
          if (c->getAttributes().getMinionActivities().isAvailable(collective, c, *activity))
            index.setHighlight(HighlightType::CREATURE_DROP);
  if (collective->getTerritory().contains(position)) {
    if (auto furniture = position.getFurniture(FurnitureLayer::MIDDLE)) {
      if (auto clickType = furniture->getClickType())
        if (auto& obj = furniture->getViewObject())
          if (index.hasObject(obj->layer()))
            index.getObject(obj->layer()).setClickAction(FurnitureClick::getClickAction(*clickType, position, furniture));
      auto workshopType = furniture->getType() == FurnitureType("FURNACE")
          ? WorkshopType("FURNACE")
          : getGame()->getContentFactory()->getWorkshopType(furniture->getType());
      if (!!workshopType)
        index.setHighlight(HighlightType::CLICKABLE_FURNITURE);
      if (chosenWorkshop && chosenWorkshop == workshopType)
        index.setHighlight(HighlightType::CLICKED_FURNITURE);
      if (draggedCreature)
        if (Creature* c = getCreature(*draggedCreature))
          if (auto task = collective->getMinionActivities().getActivityFor(collective, c, furniture->getType()))
            if (collective->isActivityGood(c, *task, true))
              index.setHighlight(HighlightType::CREATURE_DROP);
      if (collective->getMaxPopulation() <= collective->getPopulationSize() &&
          furniture->getType() == FurnitureType("TORTURE_TABLE"))
        index.setHighlight(HighlightType::TORTURE_UNAVAILABLE);
      if (auto& obj = furniture->getViewObject()) {
        if (collective->usesEfficiency(furniture)) {
          index.getObject(obj->layer()).setAttribute(ViewObject::Attribute::EFFICIENCY,
              position.getLuxuryEfficiencyMultiplier() * position.getLightingEfficiency());
          if (position.getLightingEfficiency() < 0.99)
            index.setHighlight(HighlightType::INSUFFICIENT_LIGHT);
        }
      }
    }
    if (auto furniture = position.getFurniture(FurnitureLayer::FLOOR))
      if (furniture->getType() == FurnitureType("PRISON") &&
          !position.isClosedOff(MovementType(MovementTrait::WALK).setPrisoner()))
        index.setHighlight(HighlightType::PRISON_NOT_CLOSED);
    if (auto furniture = position.getFurniture(FurnitureLayer::MIDDLE))
      if (furniture->getPopulationIncrease().requiresAnimalFence &&
          !position.isClosedOff(MovementType(MovementTrait::WALK).setFarmAnimal()))
        index.setHighlight(HighlightType::PIGSTY_NOT_CLOSED);
  }
  for (auto furniture : position.getFurniture())
    if (auto& obj = furniture->getViewObject())
      if (furniture->getLuxury() > 0 && index.hasObject(obj->layer()))
        index.getObject(obj->layer()).setAttribute(ViewObject::Attribute::LUXURY, furniture->getLuxury());
  if (auto highlight = collective->getTaskMap().getHighlightType(position))
    index.setHighlight(*highlight);
  if (collective->hasPriorityTasks(position))
    index.setHighlight(HighlightType::PRIORITY_TASK);
  if (!index.hasObject(ViewLayer::CREATURE))
    for (auto task : collective->getTaskMap().getTasks(position))
      if (auto viewId = task->getViewId())
          index.insert(ViewObject(*viewId, ViewLayer::CREATURE));
  if (position.isTribeForbidden(getTribeId()))
    index.setHighlight(HighlightType::FORBIDDEN_ZONE);
  collective->getZones().setHighlights(position, index);
  if (rectSelection
      && pos.inRectangle(Rectangle::boundingBox({rectSelection->corner1, rectSelection->corner2})))
    index.setHighlight(rectSelection->deselect ? HighlightType::RECT_DESELECTION : HighlightType::RECT_SELECTION);
#ifndef RELEASE
  if (getGame()->getOptions()->getBoolValue(OptionId::SHOW_MAP))
    if (position.getCollective())
        index.setHighlight(HighlightType::RECT_SELECTION);
#endif
  const ConstructionMap& constructions = collective->getConstructions();
  for (auto layer : ENUM_ALL(FurnitureLayer))
    if (auto f = constructions.getFurniture(position, layer))
      if (!f->isBuilt(position, layer)) {
        auto obj = furnitureFactory->getConstructionObject(f->getFurnitureType());
        if (!f->hasTask()) {
          if (collective->isDelayed(position))
            obj.setModifier(ViewObjectModifier::DANGEROUS);
          else
            obj.setModifier(ViewObjectModifier::UNPAID);
        }
        if (auto viewId = furnitureFactory->getData(f->getFurnitureType()).getSupportViewId(position))
          obj.setId(*viewId);
        index.insert(std::move(obj));
      }
  if (unknownLocations->contains(position))
    index.insert(ViewObject(ViewId("unknown_monster"), ViewLayer::TORCH2, TStringId("SURPRISE")));
}

Vec2 PlayerControl::getScrollCoord() const {
  auto currentLevel = getCurrentLevel();
  auto processTiles = [&] (const auto& tiles) -> optional<Vec2> {
    Vec2 topLeft(100000, 100000);
    Vec2 bottomRight(-100000, -100000);
    for (auto& pos : tiles)
      if (pos.isSameLevel(currentLevel)) {
        auto coord = pos.getCoord();
        topLeft.x = min(coord.x, topLeft.x);
        topLeft.y = min(coord.y, topLeft.y);
        bottomRight.x = max(coord.x, bottomRight.x);
        bottomRight.y = max(coord.y, bottomRight.y);
      }
    if (topLeft.x < 100000)
      return (topLeft + bottomRight) / 2;
    else
      return none;
  };
  if (auto pos = processTiles(collective->getTerritory().getAll()))
    return *pos;
  if (auto leader = collective->getLeaders().getFirstElement()) {
    auto keeperPos = leader[0]->getPosition();
    if (keeperPos.isSameLevel(currentLevel))
      return keeperPos.getCoord();
  }
  if (auto pos = processTiles(collective->getKnownTiles().getAll()))
    return *pos;
  return currentLevel->getBounds().middle();
}

Level* PlayerControl::getCreatureViewLevel() const {
  return getCurrentLevel();
}

void PlayerControl::controlSingle(Creature* c) {
  CHECK(getCreatures().contains(c));
  CHECK(!c->isDead());
  commandTeam(getTeams().create({c}));
}

Creature* PlayerControl::getCreature(UniqueEntity<Creature>::Id id) const {
  for (Creature* c : getCreatures())
    if (c->getUniqueId() == id)
      return c;
  return nullptr;
}

vector<Creature*> PlayerControl::getTeam(const Creature* c) {
  vector<Creature*> ret;
  for (auto team : getTeams().getActive(c))
    append(ret, getTeams().getMembers(team).filter([](auto c) { return !c->getRider(); }));
  CHECK(!ret.empty());
  return ret;
}

void PlayerControl::commandTeam(TeamId team) {
  if (!getControlled().empty())
    leaveControl();
  auto c = getTeams().getLeader(team);
  c->pushController(createMinionController(c));
  getTeams().activate(team);
  collective->freeTeamMembers(getTeams().getMembers(team));
  getView()->resetCenter();
  battleSummary = BattleSummary {};
}

void PlayerControl::toggleControlAllTeamMembers() {
  if (auto teamId = getCurrentTeam()) {
    auto members = getTeams().getMembers(*teamId);
    if (members.size() > 1) {
      if (getControlled().size() == 1) {
        getGame()->addAnalytics("fullCtrl", toString(members.size()));
        for (auto c : members)
          if (!c->isPlayer() && canControlInTeam(c))
            c->pushController(createMinionController(c));
      } else
        for (auto c : members)
          if (c->isPlayer() && c != getTeams().getLeader(*teamId))
            c->popController();
    }
  }
}

optional<PlayerMessage&> PlayerControl::findMessage(PlayerMessage::Id id){
  for (auto& elem : messages)
    if (elem.getUniqueId() == id)
      return elem;
  return none;
}

CollectiveTeams& PlayerControl::getTeams() {
  return collective->getTeams();
}

const CollectiveTeams& PlayerControl::getTeams() const {
  return collective->getTeams();
}

void PlayerControl::setScrollPos(Position pos) {
  if (!!getModel()->getMainLevelDepth(pos.getLevel())) {
    currentLevel = pos.getLevel();
    getView()->setScrollPos(pos);
  }
}

Collective* PlayerControl::getVillain(UniqueEntity<Collective>::Id id) {
  for (auto col : getGame()->getCollectives())
    if (col->getUniqueId() == id)
      return col;
  return nullptr;
}

optional<TeamId> PlayerControl::getChosenTeam() const {
  if (chosenTeam && getTeams().exists(*chosenTeam))
    return chosenTeam;
  else
    return none;
}

void PlayerControl::setChosenCreature(optional<UniqueEntity<Creature>::Id> id, TString group) {
  clearChosenInfo();
  if (id)
    chosenCreature = ChosenCreatureInfo{*id, std::move(group)};
  else
    chosenCreature = none;
}

void PlayerControl::setChosenTeam(optional<TeamId> team, optional<UniqueEntity<Creature>::Id> creature) {
  clearChosenInfo();
  chosenTeam = team;
  if (creature)
    chosenCreature = ChosenCreatureInfo{ *creature, TString()};
  else
    chosenCreature = none;
}

void PlayerControl::clearChosenInfo() {
  setChosenWorkshop(none);
  chosenCreature = none;
  chosenTeam = none;
}

void PlayerControl::setChosenWorkshop(optional<WorkshopType> type) {
  auto refreshHighlights = [&] {
    if (chosenWorkshop) {
      auto furniture = chosenWorkshop == WorkshopType("FURNACE")
          ? FurnitureType("FURNACE")
          : getGame()->getContentFactory()->workshopInfo.at(*chosenWorkshop).furniture;
      for (auto pos : collective->getConstructions().getBuiltPositions(furniture))
        pos.setNeedsRenderUpdate(true);
    }
  };
  refreshHighlights();
  if (type) {
    chosenCreature = none;
    chosenTeam = none;
  }
  chosenWorkshop = type;
  refreshHighlights();
}

void PlayerControl::minionDragAndDrop(Vec2 v, variant<TString, UniqueEntity<Creature>::Id> who) {
  PROFILE;
  Position pos(v, getCurrentLevel());
  auto handle = [&] (Creature* c) {
    c->removeEffect(LastingEffect::TIED_UP);
    c->removeEffect(LastingEffect::SLEEP);
    if (auto furniture = collective->getConstructions().getFurniture(pos, FurnitureLayer::MIDDLE))
      if (auto task = collective->getMinionActivities().getActivityFor(collective, c, furniture->getFurnitureType())) {
        if (collective->isActivityGood(c, *task, true)) {
          collective->setMinionActivity(c, *task);
          collective->setTask(c, Task::goTo(pos));
          return;
        }
      }
    for (auto task : collective->getTaskMap().getTasks(pos)) {
      if (auto activity = collective->getTaskMap().getTaskActivity(task))
        if (c->getAttributes().getMinionActivities().isAvailable(collective, c, *activity)) {
          collective->setMinionActivity(c, *activity);
          collective->returnResource(collective->getTaskMap().takeTask(c, task));
          return;
        }
    }
    PTask task = Task::goToAndWait(pos, 15_visible);
    task->setViewId(ViewId("guard_post"));
    collective->setTask(c, std::move(task));
  };
  who.visit(
      [&](const TString& group) {
        for (auto c : getMinionGroup(group))
          handle(c);
      },
      [&](UniqueEntity<Creature>::Id id) {
        if (auto c = getCreature(id))
          handle(c);
      }
  );
  pos.setNeedsRenderUpdate(true);
}

void PlayerControl::takeScreenshot() {
  takingScreenshot = true;
}

void PlayerControl::handleBanishing(Creature* c) {
  auto message = TSentence(c->isAutomaton() ? "CONFIRM_DISASSEMBLE" : "CONFIRM_BANISH", c->getName().the());
  if (getView()->yesOrNoPrompt(message)) {
    vector<Creature*> like = getMinionGroup(chosenCreature->group);
    sortMinionsForUI(like);
    if (like.size() > 1)
      for (int i : All(like))
        if (like[i] == c) {
          if (i < like.size() - 1)
            setChosenCreature(like[i + 1]->getUniqueId(), chosenCreature->group);
          else
            setChosenCreature(like[like.size() - 2]->getUniqueId(), chosenCreature->group);
          break;
        }
    collective->banishCreature(c);
  }
}

void PlayerControl::processInput(View* view, UserInput input) {
  switch (input.getId()) {
    case UserInputId::MESSAGE_INFO:
      if (auto message = findMessage(input.get<PlayerMessage::Id>())) {
        if (auto pos = message->getPosition())
          setScrollPos(*pos);
        else if (auto id = message->getCreature()) {
          if (const Creature* c = getCreature(*id))
            setScrollPos(c->getPosition());
        }
      }
      break;
    case UserInputId::DISMISS_VILLAGE_INFO: {
      auto& info = input.get<DismissVillageInfo>();
      dismissedVillageInfos.insert({info.collectiveId, info.infoText});
      break;
    }
    case UserInputId::CREATE_TEAM:
      if (Creature* c = getCreature(input.get<Creature::Id>()))
        if (canControlInTeam(c))
          getTeams().create({c});
      break;
    case UserInputId::CREATE_TEAM_FROM_GROUP: {
      optional<TeamId> team;
      for (Creature* c : getMinionGroup(input.get<TString>()))
        if (canControlInTeam(c)) {
          if (!team)
            team = getTeams().create({c});
          else
            getTeams().add(*team, c);
        }
      break;
    }
    case UserInputId::CREATURE_DRAG:
      draggedCreature = input.get<Creature::Id>();
      for (auto task : collective->getTaskMap().getAllTasks())
        if (auto pos = collective->getTaskMap().getPosition(task))
          pos->setNeedsRenderUpdate(true);
      for (auto task : ENUM_ALL(MinionActivity))
        for (auto& pos : collective->getMinionActivities().getAllPositions(collective, nullptr, task))
          pos.first.setNeedsRenderUpdate(true);
      break;
    case UserInputId::CREATURE_DRAG_DROP: {
      auto info = input.get<CreatureDropInfo>();
      minionDragAndDrop(info.pos, info.creatureId);
      draggedCreature = none;
      break;
    }
    case UserInputId::CREATURE_GROUP_DRAG_ON_MAP: {
      auto info = input.get<CreatureGroupDropInfo>();
      minionDragAndDrop(info.pos, info.group);
      draggedCreature = none;
      break;
    }
    case UserInputId::TEAM_DRAG_DROP: {
      auto& info = input.get<TeamDropInfo>();
      Position pos = Position(info.pos, getCurrentLevel());
      if (getTeams().exists(info.teamId))
        for (Creature* c : getTeams().getMembers(info.teamId)) {
          c->removeEffect(LastingEffect::TIED_UP);
          c->removeEffect(LastingEffect::SLEEP);
          PTask task = Task::goToAndWait(pos, 15_visible);
          task->setViewId(ViewId("guard_post"));
          collective->setTask(c, std::move(task));
          pos.setNeedsRenderUpdate(true);
        }
      break;
    }
    case UserInputId::CANCEL_TEAM:
      if (getChosenTeam() == input.get<TeamId>()) {
        setChosenTeam(none);
        chosenCreature = none;
      }
      getTeams().cancel(input.get<TeamId>());
      break;
    case UserInputId::SELECT_TEAM: {
      auto teamId = input.get<TeamId>();
      if (getChosenTeam() == teamId) {
        setChosenTeam(none);
        chosenCreature = none;
      } else
        setChosenTeam(teamId, getTeams().getLeader(teamId)->getUniqueId());
      break;
    }
    case UserInputId::ACTIVATE_TEAM:
      if (!getTeams().isActive(input.get<TeamId>()))
        getTeams().activate(input.get<TeamId>());
      else
        getTeams().deactivate(input.get<TeamId>());
      break;
    case UserInputId::TILE_CLICK: {
      Vec2 pos = input.get<Vec2>();
      if (pos.inRectangle(getCurrentLevel()->getBounds()))
        onSquareClick(Position(pos, getCurrentLevel()));
      break;
    }
    case UserInputId::DRAW_WORLD_MAP: getGame()->presentWorldmap(); break;
    case UserInputId::WORKSHOP: {
      auto types = collective->getWorkshops().getWorkshopsTypes();
      int index = input.get<int>();
      if (index < 0 || index > types.size())
        setChosenWorkshop(none);
      else {
        auto type = index == types.size() ? WorkshopType("FURNACE") : types[index];
        if (chosenWorkshop == type)
          setChosenWorkshop(none);
        else
          setChosenWorkshop(type);
      }
      break;
    }
    case UserInputId::WORKSHOP_ADD:
      if (chosenWorkshop) {
        int index = input.get<int>();
        if (chosenWorkshop == WorkshopType("FURNACE")) {
          auto options = getFurnaceOptions();
          if (index >= 0 && index < options.size())
           collective->getFurnace().queue(options[index].pos.removeItem(options[index].item));
        } else {
          auto& workshop = collective->getWorkshops().types.at(*chosenWorkshop);
          int index = input.get<int>();
          auto options = getWorkshopOptions(resourceIndex);
          if (index >= 0 && index < options.size()) {
            auto& item = options[index];
            auto skill = getGame()->getContentFactory()->workshopInfo.at(*chosenWorkshop)
                .getMinAttrFor(workshop.getOptions()[item.optionIndex].cost.id);
            workshop.queue(collective, item.optionIndex, skill);
            if (item.ingredient) {
              workshop.addUpgrade(workshop.getQueued().size() - 1,
                  item.ingredient->second.removeItem(item.ingredient->first));
            }
          }
        }
      }
      break;
    case UserInputId::WORKSHOP_TAB:
      if (chosenWorkshop)
        resourceIndex = input.get<int>();
      break;
    case UserInputId::WORKSHOP_UPGRADE: {
      auto& info = input.get<WorkshopUpgradeInfo>();
      auto increases = info.increases;
      if (chosenWorkshop && *chosenWorkshop != WorkshopType("FURNACE")) {
        auto& workshop = collective->getWorkshops().types.at(*chosenWorkshop);
        if (info.itemIndex < workshop.getQueued().size()) {
          auto& item = workshop.getQueued()[info.itemIndex];
          vector<pair<TString, ViewId>> allItems;
          auto getIndex = [&] (TString name, ViewId viewId) {
            for (int index : All(allItems))
              if (allItems[index] == make_pair(name, viewId))
                return index;
            allItems.push_back(make_pair(name, viewId));
            return allItems.size() - 1;
          };
          for (auto& it : getItemUpgradesFor(item.item)) {
            auto index = getIndex(it.first->getName(), it.first->getViewObject().id());
            auto itemIndex = info.itemIndex + (increases[index] - 1) % info.numItems;
            if (increases[index] > 0 && itemIndex < workshop.getQueued().size()) {
              workshop.addUpgrade(itemIndex, it.second.removeItem(it.first));
              --increases[index];
            }
          }
          vector<pair<int, int>> indexesToRemove;
          for (auto upgradeIndex : All(item.runes)) {
            auto rune = item.runes[upgradeIndex].get();
            if (rune->getUpgradeInfo()) {
              auto index = getIndex(rune->getName(), rune->getViewObject().id());
              while (increases[index] < 0) {
                indexesToRemove.push_back({upgradeIndex, info.itemIndex - increases[index] % info.numItems});
                ++increases[index];
              }
            }
          }
          for (auto index : indexesToRemove.reverse())
            Random.choose(collective->getConstructions().getAllStoragePositions().asVector())
                .dropItem(workshop.removeUpgrade(index.second, index.first));
        }
      }
      break;
    }
    case UserInputId::WORKSHOP_CHANGE_COUNT: {
      auto& info = input.get<WorkshopCountInfo>();
      if (chosenWorkshop) {
        if (*chosenWorkshop == WorkshopType("FURNACE")) {
          if (info.newCount == 0 && info.itemIndex < collective->getFurnace().getQueued().size()) {
            auto item = collective->getFurnace().unqueue(info.itemIndex);
            auto positions = collective->getStoragePositions(item->getStorageIds()).asVector();
            Random.choose(positions).dropItem(std::move(item));
          }
        } else {
          auto& workshop = collective->getWorkshops().types.at(*chosenWorkshop);
          if (info.itemIndex < workshop.getQueued().size()) {
            for (int i : Range(info.count - info.newCount))
              for (auto& upgrade : workshop.unqueue(collective, info.itemIndex))
                Random.choose(collective->getConstructions().getAllStoragePositions().asVector())
                    .dropItem(std::move(upgrade));
            for (int i : Range(info.newCount - info.count))
              workshop.queue(collective, workshop.getQueued()[info.itemIndex].indexInWorkshop,
                  workshop.getQueued()[info.itemIndex].minSkill, info.itemIndex + 1);
          }
        }
      }
      break;
    }
    case UserInputId::WORKSHOP_INCREASE_PRIORITY: {
      auto& info = input.get<WorkshopPriorityInfo>();
      if (chosenWorkshop && info.adjacentItemIndex < info.itemIndex) {
        bool maxChange = info.maxChangeFunc();
        int firstIndex = maxChange ? 0 : info.adjacentItemIndex;
        if (*chosenWorkshop == WorkshopType("FURNACE")) {
          auto& furnace = collective->getFurnace();
          furnace.changePriority(firstIndex, info.itemIndex, info.itemIndex + info.itemCount);
        }
        else {
          auto& workshop = collective->getWorkshops().types.at(*chosenWorkshop);
          workshop.changePriority(collective, firstIndex, info.itemIndex, info.itemIndex + info.itemCount); 
        }
      }
      break;
    }
    case UserInputId::WORKSHOP_DECREASE_PRIORITY: {
      auto& info = input.get<WorkshopPriorityInfo>();
      if (chosenWorkshop && info.adjacentItemIndex > info.itemIndex) {
        bool maxChange = info.maxChangeFunc();
        if (*chosenWorkshop == WorkshopType("FURNACE")) {
          auto& furnace = collective->getFurnace();
          int lastIndex = maxChange ? furnace.getQueued().size() : info.adjacentItemIndex + info.adjacentItemCount;
          furnace.changePriority(info.itemIndex, info.adjacentItemIndex, lastIndex);
        }
        else {  
          auto& workshop = collective->getWorkshops().types.at(*chosenWorkshop);
          int lastIndex = maxChange ? workshop.getQueued().size() : info.adjacentItemIndex + info.adjacentItemCount;
          workshop.changePriority(collective, info.itemIndex, info.adjacentItemIndex, lastIndex);     // ..., first, middle, last)
        }
      }
      break;
    }
    case UserInputId::LIBRARY_ADD:
      acquireTech(input.get<TechId>());
      break;
    case UserInputId::CREATURE_GROUP_BUTTON: {
      auto group = input.get<TString>();
      auto creatures = getMinionGroup(group);
      if (!creatures.empty() &&
          (!chosenCreature || getChosenTeam() || !getCreature(chosenCreature->id) || chosenCreature->group != group)) {
        setChosenTeam(none);
        sortMinionsForUI(creatures);
        setChosenCreature(creatures[0]->getUniqueId(), group);
      } else {
        setChosenTeam(none);
        chosenCreature = none;
      }
      break;
    }
    case UserInputId::CREATURE_MAP_CLICK: {
      if (Creature* c = Position(input.get<Vec2>(), getCurrentLevel()).getCreature()) {
        if (getCreatures().contains(c)) {
          if (!getChosenTeam() || !getTeams().contains(*getChosenTeam(), c))
            setChosenCreature(c->getUniqueId(), collective->getMinionGroupName(c));
          else
            setChosenTeam(*chosenTeam, c->getUniqueId());
        } else
        if (collective->getConfig().canCapturePrisoners() && collective->getTribe()->isEnemy(c))
          c->toggleCaptureOrder();
      }
      break;
    }
    case UserInputId::CREATURE_BUTTON: {
      auto chosenId = input.get<Creature::Id>();
      if (Creature* c = getCreature(chosenId)) {
        if (!!chosenCreature && (!getChosenTeam() || !getTeams().contains(*getChosenTeam(), c)))
          setChosenCreature(chosenId, chosenCreature->group);
        else
          setChosenTeam(*chosenTeam, chosenId);
      }
      else
        setChosenTeam(none);
    break;
    }
    case UserInputId::EQUIPMENT_GROUP_ACTION:
      equipmentGroupAction(input.get<EquipmentGroupAction>());
      break;
    case UserInputId::CREATURE_TASK_ACTION:
      minionTaskAction(input.get<TaskActionInfo>());
      break;
    case UserInputId::CREATURE_EQUIPMENT_ACTION:
      minionEquipmentAction(input.get<EquipmentActionInfo>());
      break;
    case UserInputId::CREATURE_PROMOTE: {
      auto info = input.get<PromotionActionInfo>();
      if (Creature* c = getCreature(info.minionId)) {
        auto factory = getGame()->getContentFactory();
        auto& promotion = factory->promotions.at(*c->getAttributes().promotionGroup)[info.promotionIndex];
        if (promotion.message)
          addMessage(TSentence(*promotion.message, c->getName().a()));
        c->addPromotion(promotion);
        collective->getDungeonLevel().consumedPromotions += c->getAttributes().promotionCost;
      }
      break;
    }
    case UserInputId::MINION_ACTION: {
      auto info = input.get<MinionActionInfo>();
      if (Creature* c = getCreature(info.id))
        switch (info.action) {
          case PlayerInfo::Action::CONTROL:
            if (getChosenTeam() && getTeams().exists(*getChosenTeam())) {
              getTeams().setLeader(*getChosenTeam(), c);
              commandTeam(*getChosenTeam());
            } else
              controlSingle(c);
            chosenCreature = none;
            setChosenTeam(none);
            break;
          case PlayerInfo::Action::RENAME:
            if (auto name = getView()->getText(TStringId("RENAME_MINION"), c->getName().first().value_or(""), maxFirstNameLength))
              c->getName().setFirst(*name);
            break;
          case PlayerInfo::Action::CONSUME:
            if (auto creatureId = getView()->chooseCreature(TStringId("CHOOSE_MINION_TO_ABSORB"),
                getConsumptionTargets(c).transform(
                    [&] (const Creature* c) { return PlayerInfo(c, getGame()->getContentFactory());}),
                TStringId("CANCEL_BUTTON")))
              if (Creature* consumed = getCreature(*creatureId))
                collective->setTask(c, Task::consume(consumed));
            break;
          case PlayerInfo::Action::LOCATE:
            setScrollPos(c->getPosition());
            break;
          case PlayerInfo::Action::BANISH:
          case PlayerInfo::Action::DISASSEMBLE:
            handleBanishing(c);
            break;
          case PlayerInfo::Action::ASSIGN_EQUIPMENT:
            collective->autoAssignEquipment(c);
            break;
          case PlayerInfo::Action::LOCK_POSITION:
            if (c->isAffected(LastingEffect::LOCKED_POSITION))
              c->removePermanentEffect(LastingEffect::LOCKED_POSITION);
            else
              c->addPermanentEffect(LastingEffect::LOCKED_POSITION);
            break;
          case PlayerInfo::Action::QUARTERS: {
            handleQuartersAction(c);
            break;
          }
        }
      break;
    }
    case UserInputId::AI_TYPE:
      minionAIAction(input.get<AIActionInfo>());
      break;
    case UserInputId::GO_TO_ENEMY:
      for (auto c : getModel()->getAllCreatures())
        if (c->getUniqueId() == input.get<Creature::Id>())
          setScrollPos(c->getPosition());
      break;
    case UserInputId::ADD_GROUP_TO_TEAM: {
      auto info = input.get<TeamGroupInfo>();
      for (Creature* c : getMinionGroup(info.group))
        if (getTeams().exists(info.team) && !getTeams().contains(info.team, c) && canControlInTeam(c))
          getTeams().add(info.team, c);
      break;
    }
    case UserInputId::ADD_TO_TEAM: {
      auto info = input.get<TeamCreatureInfo>();
      if (Creature* c = getCreature(info.creatureId))
        if (getTeams().exists(info.team) && !getTeams().contains(info.team, c) && canControlInTeam(c))
          getTeams().add(info.team, c);
      break;
    }
    case UserInputId::REMOVE_FROM_TEAM: {
      auto info = input.get<TeamCreatureInfo>();
      if (Creature* c = getCreature(info.creatureId))
        if (getTeams().exists(info.team) && getTeams().contains(info.team, c)) {
          getTeams().remove(info.team, c);
          if (getTeams().exists(info.team)) {
            if (chosenCreature && chosenCreature->id == info.creatureId)
              setChosenTeam(info.team, getTeams().getLeader(info.team)->getUniqueId());
          } else
            chosenCreature = none;
        }
      break;
    }
    case UserInputId::IMMIGRANT_ACCEPT: {
      int index = input.get<int>();
      if (index < 0)
        acceptPrisoner(index);
      else {
        auto available = collective->getImmigration().getAvailable();
        if (auto info = getReferenceMaybe(available, index))
          if (auto sound = info->get().getInfo().getSound())
            getView()->addSound(*sound);
        collective->getImmigration().accept(index);
      }
      break;
    }
    case UserInputId::IMMIGRANT_REJECT: {
      int index = input.get<int>();
      if (index < 0)
        rejectPrisoner(index);
      else {
        collective->getImmigration().rejectIfNonPersistent(index);
      }
      break;
    }
    case UserInputId::IMMIGRANT_AUTO_ACCEPT: {
      int id = input.get<int>();
      if (id >= 0) { // Otherwise the player has clicked the dummy prisoner element
        if (!!collective->getImmigration().getAutoState(id))
          collective->getImmigration().setAutoState(id, none);
        else
          collective->getImmigration().setAutoState(id, ImmigrantAutoState::AUTO_ACCEPT);
      }
      break;
    }
    case UserInputId::IMMIGRANT_AUTO_REJECT: {
      int id = input.get<int>();
      if (id >= 0) { // Otherwise the player has clicked the dummy prisoner element
        if (!!collective->getImmigration().getAutoState(id))
          collective->getImmigration().setAutoState(id, none);
        else
          collective->getImmigration().setAutoState(id, ImmigrantAutoState::AUTO_REJECT);
      }
      break;
    }
    case UserInputId::RECT_SELECTION: {
      auto& info = input.get<BuildingClickInfo>();
      if (info.doubleClick) {
        handleSelection(info.pos, buildInfo[info.building], false, true);
      }
      else if (buildInfo[info.building].canSelectRectangle()) {
        updateSelectionSquares();
        if (rectSelection) {
          rectSelection->corner2 = info.pos;
        } else {
          rectSelection = CONSTRUCT(SelectionInfo, c.corner1 = c.corner2 = info.pos;);
          selection = Selection::NONE;
          handleSelection(info.pos, buildInfo[info.building], true, false);
          rectSelection->deselect = (selection == Selection::DESELECT);
          selection = Selection::NONE;
        }
        updateSelectionSquares();
      } else
        handleSelection(info.pos, buildInfo[info.building], false, false);
      break;
    }
    case UserInputId::RECT_CONFIRM:
      if (rectSelection) {
        auto box = Rectangle::boundingBox({rectSelection->corner1, rectSelection->corner2});
        for (Vec2 v : box) {
          auto w = v;
          if (rectSelection->corner1.x != box.left())
            w.x = box.left() + box.right() - 1 - w.x;
          if (rectSelection->corner1.y != box.top())
            w.y = box.top() + box.bottom() - 1 - w.y;
          std::cout << w << endl;
          handleSelection(w, buildInfo[input.get<BuildingClickInfo>().building], false, false);
        }
      }
      FALLTHROUGH;
    case UserInputId::RECT_CANCEL:
      updateSelectionSquares();
      rectSelection = none;
      selection = Selection::NONE;
      break;
    case UserInputId::VILLAGE_ACTION: {
      auto& info = input.get<VillageActionInfo>();
      if (Collective* village = getVillain(info.id))
        switch (info.action) {
          case VillageAction::TRADE:
            handleTrading(village);
            break;
          case VillageAction::PILLAGE:
            handlePillage(village);
            break;
        }
      break;
    }
    case UserInputId::SHOW_HISTORY:
      PlayerMessage::presentMessages(getView(), messageHistory);
      break;
    case UserInputId::CHEAT_ATTRIBUTES:
      for (auto& resource : getGame()->getContentFactory()->resourceInfo)
        collective->returnResource(CostInfo(resource.first, 1000));
      collective->getDungeonLevel().increaseLevel();
      break;
    case UserInputId::TUTORIAL_CONTINUE:
      if (tutorial)
        tutorial->continueTutorial(getGame());
      break;
    case UserInputId::TUTORIAL_GO_BACK:
      if (tutorial)
        tutorial->goBack();
      break;
    case UserInputId::EXIT: getGame()->exitAction(); return;
    case UserInputId::IDLE: break;
    case UserInputId::DISMISS_NEXT_WAVE:
      if (auto& enemies = getModel()->getExternalEnemies())
        if (auto nextWave = enemies->getNextWave())
          dismissedNextWaves.insert(enemies->getNextWaveIndex());
      break;
    case UserInputId::DISMISS_WARNING_WINDOW:
      lastWarningDismiss = getModel()->getLocalTime();
      break;
    case UserInputId::SCROLL_TO_HOME:
      setScrollPos(collective->getLeaders()[0]->getPosition());
      break;
    case UserInputId::SCROLL_STAIRS:
      scrollStairs(input.get<int>());
      break;
    case UserInputId::TAKE_SCREENSHOT:
      getView()->dungeonScreenshot(input.get<Vec2>());
      getGame()->addEvent(EventInfo::RetiredGame{});
      getGame()->setExitInfo(GameSaveType::RETIRED_SITE);
      break;
    case UserInputId::CANCEL_SCREENSHOT:
      takingScreenshot = false;
      break;
    default:
      break;
  }
}

void PlayerControl::handleQuartersAction(Creature* c) {
  using QI = Zones::QuartersInfo;
  const auto& finalList = collective->getZones().getAllQuarters(c->getUniqueId());
  if (finalList.empty()) return;

  ScriptedUIDataElems::Record data;
  double currentLuxury = collective->getZones().getQuarters(c->getUniqueId()).empty() == false
    ? finalList[0].luxury // selected creature's quarters will be on top (if it has one)
    : 0.0; // otherwise 0
  double reqLuxury = getRequiredLuxury(c->getCombatExperience(false, false));
  if (reqLuxury > currentLuxury)
    data.elems["currInsuffLux"] = TString("placeholder"_s);
  data.elems["title"] = TString(TStringId("QUARTERS_TITLE"));
  data.elems["currentLuxText"] = TString(TStringId("QUARTERS_CURRENT_LUXURY_LABEL"));
  data.elems["currentLuxury"] = TString(TSentence("QUARTERS_CURRENT_LUXURY", TString(toString(currentLuxury)), TString(getRequiredLuxury(c->getCombatExperience(false, false)))));
  data.elems["quarters_header"] = TString(TStringId("QUARTERS_DETAILS_LABEL"));
  data.elems["owner_header"] = TString(TStringId("QUARTERS_OWNER_LABEL"));
  data.elems["goto_header"] = TString(TStringId("GOTO_BUTTON_LABEL"));
  data.elems["goto_text"] = TString(TStringId("GOTO_BUTTON_LABEL"));
  
  ScriptedUIDataElems::List list;
  int idx = 0;
  int locateIdx = -1, assignIdx = -1;
  for (const QI& q : finalList) {              
    ScriptedUIDataElems::Record r;
    if (reqLuxury > q.luxury)
      r.elems["insuffLuxury"] = TString(""_s);
    r.elems["luxury"] = combineWithNoSpace(TStringId("QUARTERS_LUXURY_LABEL"), TString(toString(q.luxury)));
    const auto& level = q.positions.begin()->getLevel();
    r.elems["level"] = combineWithNoSpace(TStringId("QUARTERS_LEVEL_LABEL"), TString(level->depth));
    TString ownerLabel = TString(TStringId("QUARTERS_MENU_UNASSIGNED_LABEL"));
    if (q.id) {
      const auto& quartersOwner = getCreature(*q.id);
      if (q.id == c->getUniqueId()) {
        r.elems["thisOwner"] = TString("placeholder"_s);
        r.elems["current"] = TString(TStringId("QUARTERS_CURRENT_LABEL"));
        ownerLabel = getCreature(*q.id)->getName().aOrTitle();
      }
      else {
        ownerLabel = quartersOwner->getName().aOrTitle();
      }
      r.elems["view_id"] = quartersOwner->getViewObject().getViewIdList();
    }
    r.elems["owner"] = ownerLabel;
    auto callbackAssign = [&assignIdx, idx = idx] { assignIdx = idx; return true; };
    std::function<bool()> callbackLocate = [&locateIdx, idx = idx] { locateIdx = idx; return true; };
    r.elems["callback"] = ScriptedUIDataElems::Callback { callbackAssign, callbackLocate };
    list.push_back(std::move(r));
    ++idx;
  }
  data.elems["elems"] = std::move(list);
  
  ScriptedUIState state;
  getView()->scriptedUI("quarters_menu", data, state);

  if (assignIdx > -1)
    collective->assignQuarters(c, *finalList[assignIdx].positions.begin());
  else if (locateIdx > -1)
    setScrollPos(*finalList[locateIdx].positions.begin());
}

void PlayerControl::scrollStairs(int dir) {
  if (!currentLevel)
    currentLevel = getModel()->getGroundLevel();
  int index = *getModel()->getMainLevelDepth(currentLevel);
  index = getModel()->getMainLevelsDepth().clamp(index + dir);
  currentLevel = getModel()->getMainLevel(index);
  getView()->updateView(this, false);
  CHECK(currentLevel);
}

vector<Creature*> PlayerControl::getConsumptionTargets(Creature* consumer) const {
  vector<Creature*> ret;
  for (Creature* c : getCreatures())
    if (consumer->canConsume(c) && (collective->getLeaders().size() > 1 || !collective->hasTrait(c, MinionTrait::LEADER)))
      ret.push_back(c);
  return ret;
}

void PlayerControl::updateSelectionSquares() {
  if (rectSelection)
    for (Vec2 v : Rectangle::boundingBox({rectSelection->corner1, rectSelection->corner2}))
      Position(v, getCurrentLevel()).setNeedsRenderUpdate(true);
}

void PlayerControl::handleDestructionOrder(Position position, HighlightType highlightType,
    DestroyAction destructionType, bool dryRun) {
  bool markedToDig = collective->getTaskMap().getHighlightType(position) == highlightType;
  if (markedToDig && selection != Selection::SELECT) {
    if (!dryRun) {
      collective->cancelMarkedTask(position);
      getView()->addSound(SoundId("DIG_UNMARK"));
    }
    selection = Selection::DESELECT;
  } else
  if (!markedToDig && selection != Selection::DESELECT) {
    if (auto furniture = position.getFurniture(FurnitureLayer::MIDDLE))
      if (furniture->canDestroy(destructionType)) {
        if (!dryRun) {
          collective->orderDestruction(position, destructionType);
          getView()->addSound(SoundId("DIG_MARK"));
        }
        selection = Selection::SELECT;
      }
  }
}

void PlayerControl::handleSelection(Vec2 pos, const BuildInfo& building, bool dryRun, bool doubleClick) {
  PROFILE;
  Position position(pos, getCurrentLevel());
  for (auto& req : building.requirements)
    if (!BuildInfo::meetsRequirement(collective, req))
      return;
  if (position.isUnavailable())
    return;
  handleSelection(position, building.type, dryRun, doubleClick);
}

void PlayerControl::handleSelection(Position position, const BuildInfoTypes::BuildType& building, bool dryRun,
    bool doubleClick) {
  building.visit<void>(
    [&](const BuildInfoTypes::ImmediateDig&) {
      auto furniture = position.modFurniture(FurnitureLayer::MIDDLE);
      if (furniture && furniture->isWall())
        furniture->destroy(position, DestroyAction::Type::BASH);
      for (Position v : position.getRectangle(Rectangle::centered(3))) {
        addToMemory(v);
        collective->addKnownTile(v);
      }
    },
    [&](const BuildInfoTypes::PlaceItem&) {
      ItemType item;
      if (auto num = getView()->getNumber(TStringId("ENTER_AMOUNT"), Range(1, 10000), 1))
        if (auto input = getView()->getText(TStringId("ENTER_ITEM_TYPE"), "", 100)) {
          if (auto error = PrettyPrinting::parseObject(item, *input))
            getView()->presentText(TString("Sorry"_s), TString("Couldn't parse \"" + *input + "\": " + *error));
          else {
            position.dropItems(item.get(*num, getGame()->getContentFactory()));
          }
        }
    },
    [&](const BuildInfoTypes::DestroyLayers& layers) {
      for (auto layer : layers) {
        auto f = collective->getConstructions().getFurniture(position, layer);
        if (f && !f->isBuilt(position, layer)) {
          collective->removeUnbuiltFurniture(position, layer);
          getView()->addSound(SoundId("DIG_UNMARK"));
          selection = Selection::SELECT;
        } else
        if (collective->getKnownTiles().isKnown(position) && !position.isBurning()) {
          selection = Selection::SELECT;
          optional<Position> otherPos;
          if (auto f = position.getFurniture(layer))
            otherPos = f->getSecondPart(position);
          collective->destroyOrder(position, layer);
          if (auto f = position.getFurniture(layer))
            if (f->canRemoveInstantly())
              position.removeFurniture(f);
          updateSquareMemory(position);
          if (otherPos)
            updateSquareMemory(*otherPos);
        } else
          if (auto f = position.getFurniture(layer))
            if (f->canRemoveInstantly())
              position.removeFurniture(f);
      }
    },
    [&](const BuildInfoTypes::ForbidZone) {
      if (position.isTribeForbidden(getTribeId()) && selection != Selection::SELECT) {
        if (!dryRun)
          position.allowMovementForTribe(getTribeId());
        selection = Selection::DESELECT;
      } else
      if (!position.isTribeForbidden(getTribeId()) && selection != Selection::DESELECT) {
        if (!dryRun)
          position.forbidMovementForTribe(getTribeId());
        selection = Selection::SELECT;
      }
    },
    [&](const BuildInfoTypes::CutTree&) {
      if (doubleClick) {
        auto furniture = position.getFurniture(FurnitureLayer::MIDDLE);
        if (furniture && furniture->isClearFogOfWar() && furniture->canDestroy(DestroyAction::Type::CUT)) {
          handleDestructionOrder(position, HighlightType::CUT_TREE, DestroyAction::Type::CUT, false);
          selection = Selection::NONE;
          bool wasSelected = collective->getTaskMap().getHighlightType(position) == HighlightType::CUT_TREE;
          addResourceRecursively(position, !wasSelected, HighlightType::CUT_TREE, DestroyAction::Type::CUT);
        }
      } else
        handleDestructionOrder(position, HighlightType::CUT_TREE, DestroyAction::Type::CUT, dryRun);
    },
    [&](const BuildInfoTypes::Dig&) {
      if (doubleClick) {
        auto furniture = position.getFurniture(FurnitureLayer::MIDDLE);
        if (furniture && furniture->isClearFogOfWar() && furniture->canDestroy(DestroyAction::Type::DIG)) {
          handleDestructionOrder(position, HighlightType::DIG, DestroyAction::Type::DIG, false);
          selection = Selection::NONE;
          bool wasSelected = collective->getTaskMap().getHighlightType(position) == HighlightType::DIG;
          addResourceRecursively(position, !wasSelected, HighlightType::DIG, DestroyAction::Type::DIG);
        }
      } else
        handleDestructionOrder(position, HighlightType::DIG, DestroyAction::Type::DIG, dryRun);
    },
    [&](const BuildInfoTypes::FillPit&) {
      if (auto f = position.getFurniture(FurnitureLayer::MIDDLE))
        if (auto t = f->getTickType())
          if (t->contains<FurnitureTickTypes::Pit>())
            handleDestructionOrder(position, HighlightType::DIG, DestroyAction::Type::FILL, dryRun);
    },
    [&](const BuildInfoTypes::Chain& c) {
      for (auto& elem : c)
        handleSelection(position, elem, dryRun, doubleClick);
    },
    [&](ZoneId zone) {
      auto& zones = collective->getZones();
      if (zones.isZone(position, zone) && selection != Selection::SELECT) {
        if (!dryRun)
          collective->eraseZone(position, zone);
        selection = Selection::DESELECT;
      } else if (selection != Selection::DESELECT && !zones.isZone(position, zone) &&
          collective->getKnownTiles().isKnown(position) &&
          zones.canSet(position, zone, collective)) {
        if (!dryRun)
          collective->setZone(position, zone);
        selection = Selection::SELECT;
      }
    },
    [&](BuildInfoTypes::ClaimTile) {
      if (!dryRun && collective->canClaimSquare(position))
        collective->claimSquare(position);
    },
    [&](BuildInfoTypes::UnclaimTile) {
      if (!dryRun) {
        collective->unclaimSquare(position);
        if (NOTNULL(position.getFurniture(FurnitureLayer::GROUND))->getViewObject()->id() == ViewId("keeper_floor")) {
          auto& obj = position.modFurniture(FurnitureLayer::GROUND)->getViewObject();
          obj->setId(ViewId("floor"));
          obj->setDescription(TStringId("FLOOR"));
          position.setNeedsRenderAndMemoryUpdate(true);
          updateSquareMemory(position);
        }
      }
    },
    [&](BuildInfoTypes::Dispatch) {
      if (!dryRun)
        collective->setPriorityTasks(position);
    },
    [&](BuildInfoTypes::PlaceMinion) {
      auto& factory = getGame()->getContentFactory()->getCreatures();
      vector<PCreature> allCreatures = factory.getAllCreatures().transform(
          [this, &factory](CreatureId id){ return factory.fromId(id, getTribeId()); });
      if (auto id = getView()->chooseCreature(TStringId("CHOOSE_CREATURE_TO_PLACE"),
          allCreatures.transform([&](auto& c) { return PlayerInfo(c.get(), this->getGame()->getContentFactory()); }),
          TStringId("CANCEL_BUTTON"))) {
        for (auto& c : allCreatures)
          if (c->getUniqueId() == *id) {
            collective->addCreature(std::move(c), position, {MinionTrait::FIGHTER});
          }
      }
    },
    [&](const BuildInfoTypes::Furniture& info) {
      auto layer = getGame()->getContentFactory()->furniture.getData(info.types[0]).getLayer();
      auto currentPlanned = collective->getConstructions().getFurniture(position, layer);
      if (currentPlanned && currentPlanned->isBuilt(position, layer))
        currentPlanned = none;
      int nextIndex = 0;
      if (currentPlanned) {
        if (auto currentIndex = info.types.findElement(currentPlanned->getFurnitureType()))
          nextIndex = *currentIndex + 1;
        else
          return;
      }
      bool removed = false;
      if (!!currentPlanned && selection != Selection::SELECT) {
        if (!dryRun)
          collective->removeUnbuiltFurniture(position, layer);
        removed = true;
      }
      while (nextIndex < info.types.size() && !collective->canAddFurniture(position, info.types[nextIndex]))
        ++nextIndex;
      int totalCount = 0;
      for (auto type : info.types)
        totalCount += collective->getConstructions().getTotalCount(type);
      if (nextIndex < info.types.size() && selection != Selection::DESELECT &&
          (!info.limit || *info.limit > totalCount)) {
        if (!dryRun) {
          collective->addFurniture(position, info.types[nextIndex], info.cost, info.noCredit);
          getView()->addSound(SoundId("ADD_CONSTRUCTION"));
        }
        selection = Selection::SELECT;
      } else if (removed) {
        if (!dryRun)
          getView()->addSound(SoundId("DIG_UNMARK"));
        selection = Selection::DESELECT;
      }
    }
  );
}

void PlayerControl::addResourceRecursively(Position pos, bool select,
    HighlightType highlightType, DestroyAction::Type destroyAction) {
  bool wasSelected = collective->getTaskMap().getHighlightType(pos) == highlightType;
  if (wasSelected == select)
    return;
  handleDestructionOrder(pos, highlightType, destroyAction, false);
  auto myType = pos.getFurniture(FurnitureLayer::MIDDLE)->getType();
  for (auto v : pos.neighbors4())
    if (auto f = v.getFurniture(FurnitureLayer::MIDDLE))
      if (f->getType() == myType)
        addResourceRecursively(v, select, highlightType, destroyAction);
}

void PlayerControl::onSquareClick(Position pos) {
  if (auto furniture = pos.getFurniture(FurnitureLayer::MIDDLE)) {
    if (auto link = pos.getLandingLink()) {
      auto otherLevel = getModel()->getLinkedLevel(pos.getLevel(), *link);
      if (!!getModel()->getMainLevelDepth(otherLevel)) {
        currentLevel = otherLevel;
        auto newPos = currentLevel->getLandingSquares(*link)[0];
        if (newPos.getCoord() != pos.getCoord())
          setScrollPos(newPos);
        getView()->updateView(this, false);
      }
    }
    if (collective->getTerritory().contains(pos)) {
      if (furniture->getClickType()) {
        furniture->click(pos); // this can remove the furniture
        updateSquareMemory(pos);
      } else
      if (furniture->getType() == FurnitureType("FURNACE"))
        setChosenWorkshop(WorkshopType("FURNACE"));
      else
      if (auto workshopType = getGame()->getContentFactory()->getWorkshopType(furniture->getType()))
        if (collective->getWorkshops().getWorkshopsTypes().contains(*workshopType))
          setChosenWorkshop(*workshopType);
    }
  }
  if (collective->getZones().isZone(pos, ZoneId::QUARTERS)) {
    auto creatures = getCreatures().filter([this] (auto c) { return collective->minionCanUseQuarters(c); });
    auto& zones = collective->getZones();
    auto hasQuarters = [this] (auto c) { return !collective->getZones().getQuarters(c->getUniqueId()).empty(); };
    sort(creatures.begin(), creatures.end(), [hasQuarters](auto c1, auto c2) {
      auto h1 = hasQuarters(c1);
      auto h2 = hasQuarters(c2);
      if (h1 && !h2)
        return false;
      if (h2 && !h1)
        return true;
      if (!h1 && !h2) {
        auto needs1 = c1->getCombatExperience(false, false) > c1->getCombatExperienceCap();
        auto needs2 = c2->getCombatExperience(false, false) > c2->getCombatExperienceCap();
        if (needs1 && !needs2)
          return true;
        if (needs2 && !needs1)
          return false;
      }
      return c1->getUniqueId() > c2->getUniqueId();
    });
    vector<PlayerInfo> minions;
    for (auto c : creatures) {
      minions.push_back(PlayerInfo(c, getGame()->getContentFactory()));
      if (!hasQuarters(c))
        minions.back().description = TStringId("NO_QUARTERS_ASSIGNED_YET");
      else
        minions.back().description = TString();
    }
    if (auto id = getView()->chooseCreature(TStringId("ASSIGN_QUARTERS_TO"), minions, TStringId("CANCEL_BUTTON")))
      if (auto c = getCreature(*id))
        collective->assignQuarters(c, pos);
  }
}

optional<PlayerControl::QuartersInfo> PlayerControl::getQuarters(Vec2 pos) const {
  optional<QuartersInfo> quartersInfo;
  auto level = getCurrentLevel();
  auto& zones = collective->getZones();
  if (auto info = zones.getQuartersInfo(Position(pos, level))) {
    HashSet<Vec2> v;
    double luxury = 0;
    for (auto& pos : info->positions)
      if (pos.getLevel() == level) {
        v.insert(pos.getCoord());
        luxury += pos.getTotalLuxuryPlusWalls();
      }
    optional<ViewIdList> viewId;
    optional<TString> name;
    if (info->id)
      if (auto c = getCreature(*info->id)) {
        viewId = c->getViewObject().getViewIdList();
        name = c->getName().aOrTitle();
      }
    quartersInfo = QuartersInfo {
      v,
      viewId,
      name,
      luxury
    };
  }
  return quartersInfo;
}

double PlayerControl::getAnimationTime() const {
  double localTime = getModel()->getLocalTimeDouble();
  // Snap all animations into place when the clock is paused and pausing animations has stopped.
  // This ensures that a creature that the player controlled that's an extra move ahead
  // will be positioned properly.
  if (getView()->isClockStopped() && localTime >= trunc(localTime) + pauseAnimationRemainder)
    return 10000000;
  return localTime;
}

PlayerControl::CenterType PlayerControl::getCenterType() const {
  return CenterType::NONE;
}

const vector<Vec2>& PlayerControl::getUnknownLocations(const Level*) const {
  PROFILE;
  return unknownLocations->getOnLevel(getCurrentLevel());
}

optional<Vec2> PlayerControl::getSelectionSize() const {
  return rectSelection.map([](const SelectionInfo& s) { return s.corner1 - s.corner2; });
}

static optional<vector<Vec2>> getCreaturePath(Creature* c, Vec2 target, Level* level) {
  auto movement = c->getMovementType();
  auto from = c->getPosition();
  auto to = Position(target, level);
  if (!to.isValid() || !from.isSameModel(to))
    return none;
  if (from.getLevel() != level) {
    if (auto stairs = to.getStairsTo(from, c->getMovementType()))
      from = stairs->first;
    else
      return none;
  }
  LevelShortestPath path(from, movement, to, 0);
  return path.getPath().transform([](auto& pos) { return pos.getCoord(); });
};

vector<vector<Vec2>> PlayerControl::getPathTo(UniqueEntity<Creature>::Id id, Vec2 v) const {
  if (auto creature = getCreature(id))
    if (auto path = getCreaturePath(creature, v, getCurrentLevel()))
      return {*path};
  return {};
}

vector<vector<Vec2>> PlayerControl::getGroupPathTo(const TString& group, Vec2 v) const {
  vector<vector<Vec2>> ret;
  auto level = getCurrentLevel();
  for (auto c : getMinionGroup(group))
    if (auto path = getCreaturePath(c, v, level))
      ret.push_back(*path);
  return ret;
}

vector<vector<Vec2>> PlayerControl::getTeamPathTo(TeamId teamId, Vec2 v) const {
  auto teams = getTeams();
  auto level = getCurrentLevel();
  vector<vector<Vec2>> ret;
  if (teams.exists(teamId))
    for (auto c : getTeams().getMembers(teamId))
      if (auto path = getCreaturePath(c, v, level))
        ret.push_back(*path);
  return ret;
}

vector<Vec2> PlayerControl::getHighlightedPathTo(Vec2 v) const {
  auto level = getCurrentLevel();
  if (auto c = Position(v, level).getCreature())
    if (canSee(c) && !isEnemy(c)) {
      auto res = c->getCurrentPath()
          .filter([&](auto& pos) { return pos.getLevel() == level; } )
          .transform([&](auto& pos) { return pos.getCoord(); } );
      if (res.size() > 1)
        res.pop_back();
      return res;
    }
  return {};
}

CreatureView::PlacementInfo PlayerControl::canPlaceItem(Vec2 pos, int index) const {
  Position position(pos, getCurrentLevel());
  auto& building = buildInfo[index];
  if (auto f = building.type.getReferenceMaybe<BuildInfoTypes::Furniture>()) {
    vector<Vec2> inGreen;
    vector<Vec2> inRed;
    if (f->types[0].data() == "ARCHERY_RANGE"_s) {
      for (auto dir : Vec2::directions4()) {
        auto isGood = [&] {
          for (int i : Range(1, Task::archeryRangeDistance + 1)) {
            auto checkPos = position.plus(dir * i);
            if (!collective->getKnownTiles().isKnown(checkPos) || checkPos.stopsProjectiles(VisionId::NORMAL))
              return false;
          }
          return true;
        }();
        auto targetPos = position.getCoord() + dir * Task::archeryRangeDistance;
        if (isGood) {
          inGreen.push_back(targetPos);
          inRed.clear();
        } else if (inGreen.empty())
          inRed.push_back(targetPos);
      }
    }
    for (auto& type : f->types)
      if (collective->canAddFurniture(position, type))
        return PlacementInfo{true, inGreen, inRed};
    auto& factory = getGame()->getContentFactory()->furniture;
    auto layer = factory.getData(f->types[0]).getLayer();
    auto currentPlanned = collective->getConstructions().getFurniture(position, layer);
    if (currentPlanned && !currentPlanned->isBuilt(position, layer) && f->types.contains(currentPlanned->getFurnitureType()))
      return PlacementInfo{true, inGreen, inRed};
    return PlacementInfo{false, inGreen, inRed};
  }
  return PlacementInfo{true, {}, {}};
}

void PlayerControl::addToMemory(Position pos) {
  PROFILE;
  if (!pos.needsMemoryUpdate())
    return;
 if (auto ground = pos.getGroundBelow())
    addToMemory(ground->first);
  pos.setNeedsMemoryUpdate(false);
  ViewIndex index;
  getSquareViewIndex(pos, true, index);
  memory->update(pos, index);
}

void PlayerControl::checkKeeperDanger() {
  if (!getGame()->getOptions()->getBoolValue(OptionId::KEEPER_WARNING))
    return;
  auto controlled = getControlled();
  for (auto c : controlled)
    if (collective->hasTrait(c, MinionTrait::LEADER))
      return;
  for (auto keeper : collective->getLeaders()) {
    auto prompt = [&] (TSentence reason) {
      reason.params.push_back(collective->getLeaders().size() > 1 ? keeper->getName().a() : keeper->getName().the());
      auto res = getView()->multiChoice(
          capitalFirst(std::move(reason)),
          {TStringId("TAKE_CONTROL"), TStringId("DISMISS_FOR_200_TURNS"), TStringId("DISMISS_AND_DONT_ASK_AGAIN")});
      if (res == 0)
        controlSingle(keeper);
      else if (res == 2)
        getGame()->getOptions()->setValue(OptionId::KEEPER_WARNING, 0);
      else
        nextKeeperWarning = getGame()->getGlobalTime() +
            TimeInterval(getGame()->getOptions()->getIntValue(OptionId::KEEPER_WARNING_TIMEOUT));
    };
    if (!keeper->isDead() && !controlled.contains(keeper) &&
        nextKeeperWarning < collective->getGlobalTime()) {
      if (auto lastCombatIntent = keeper->getLastCombatIntent())
        if (lastCombatIntent->isHostile() && lastCombatIntent->time > getGame()->getGlobalTime() - 5_visible)
          return prompt(TSentence("IS_ENGAGED_IN_A_FIGHT_WITH", lastCombatIntent->attacker->getName().a()));
      if (keeper->isAffected(LastingEffect::POISON))
        return prompt(TSentence("IS_SUFFERING_FROM_POISONING"));
      else if (keeper->isAffected(BuffId("BLEEDING")))
        return prompt(TSentence("IS_BLEEDING"));
      else if (keeper->getBody().isWounded() &&
          leaderWoundedTime.getOrElse(keeper, -100_local) > getModel()->getLocalTime() - 10_visible)
        return prompt(TSentence("IS_WOUNDED"));
    }
  }
}

void PlayerControl::dismissKeeperWarning() {
}

void PlayerControl::considerNightfallMessage() {
  /*if (getGame()->getSunlightInfo().getState() == SunlightState::NIGHT) {
    if (!isNight) {
      addMessage(PlayerMessage("Night is falling. Killing enemies in their sleep yields double mana.",
            MessagePriority::HIGH));
      isNight = true;
    }
  } else
    isNight = false;*/
}

bool PlayerControl::canAllyJoin(Creature* ally) const {
  for (auto c : ally->getVisibleCreatures())
    if (getCreatures().contains(c) && ally->getPosition().isConnectedTo(c->getPosition(), {MovementTrait::WALK}))
      return true;
  return false;
}

void PlayerControl::considerTogglingCaptureOrderOnMinions() const {
  for (auto c : getCreatures())
    c->setCaptureOrder(false);
  auto toCapture = collective->getCreatures(MinionTrait::FIGHTER).filter([&](auto c) { return !collective->hasTrait(c, MinionTrait::LEADER); });
  sort(toCapture.begin(), toCapture.end(),
      [](auto c1, auto c2) { return c1->getCombatExperience(true, false) > c2->getCombatExperience(true, false); });
  if (toCapture.size() > 10)
    toCapture.resize(10);
/*  for (auto c : collective->getCreatures(MinionTrait::LEADER))
    if (!toCapture.contains(c))
      toCapture.push_back(c);*/
  for (auto c : toCapture)
    for (auto col : c->getPosition().getModel()->getCollectives())
      if (col != collective && col->getConfig().canCapturePrisoners() && !col->isConquered())
        for (auto pos : col->getTerritory().getPillagePositions())
          if (pos.getCreature() == c)
            c->setCaptureOrder(true);
}

void PlayerControl::update(bool currentlyActive) {
  considerTogglingCaptureOrderOnMinions();
  vector<Creature*> addedCreatures;
  vector<Level*> currentLevels {getCurrentLevel()};
  for (auto c : getControlled())
    if (!currentLevels.contains(c->getLevel()))
      currentLevels.push_back(c->getLevel());
  for (Level* l : currentLevels)
    for (Creature* c : l->getAllCreatures())
      if (!getCreatures().contains(c) && c->getTribeId() == getTribeId() && canSee(c) && !isEnemy(c) && canAllyJoin(c)) {
        if (!collective->wasBanished(c) && !c->getBody().isFarmAnimal() && c->getAttributes().getCanJoinCollective()) {
          addedCreatures.push_back(c);
          collective->addCreature(c, {MinionTrait::FIGHTER, MinionTrait::NO_LIMIT});
          for (auto controlled : getControlled())
            if (canControlInTeam(c)
                && c->getPosition().isSameLevel(controlled->getPosition())
                && !collective->hasTrait(c, MinionTrait::SUMMONED)) {
              addToCurrentTeam(c);
              controlled->privateMessage(PlayerMessage(TSentence("JOINS_YOUR_TEAM", c->getName().a()),
                  MessagePriority::HIGH));
              break;
            }
        } else
          if (c->getBody().isFarmAnimal())
            collective->addCreature(c, {MinionTrait::FARM_ANIMAL, MinionTrait::NO_LIMIT});
      }
  if (!addedCreatures.empty()) {
    collective->addNewCreatureMessage(addedCreatures);
  }
}

Level* PlayerControl::getCurrentLevel() const {
  if (!currentLevel)
    return getModel()->getGroundLevel();
  else
    return currentLevel;
}

bool PlayerControl::isConsideredAttacking(const CollectiveAttack& attack) {
  auto enemy = attack.getAttacker();
  auto leader = attack.getCreatures()[0];
  if (enemy && enemy->getModel() == getModel())
    return canSee(leader) && (collective->getTerritory().contains(leader->getPosition()) ||
        collective->getTerritory().getStandardExtended().contains(leader->getPosition()));
  else
    return canSee(leader) && leader->getLevel()->getModel() == getModel();
}

void PlayerControl::updateUnknownLocations() {
  vector<Position> locations;
  for (auto col : getGame()->getCollectives())
    if (!collective->isKnownVillainLocation(col) && !col->isConquered())
      if (auto& pos = col->getTerritory().getCentralPoint())
        locations.push_back(*pos);
  unknownLocations->update(locations);
}

void PlayerControl::considerTransferingLostMinions() {
  if (getGame()->getCurrentModel() == getModel())
    for (auto c : copyOf(getCreatures()))
      if (c->getPosition().getModel() != getModel() && !c->isAffected(LastingEffect::STUNNED)) {
        if (auto r = c->getRider())
          if (!getCreatures().contains(r))
            r->tryToDismount();
        getGame()->transferCreature(c, getModel());
      }
}

void PlayerControl::considerAllianceAttack() {
  if (allianceAttack) {
    auto message = [&] {
      for (auto col : *allianceAttack)
        for (auto leader : col->getLeaders())
          if (!collective->getTerritory().getExtended(10).contains(leader->getPosition()))
            return false;
      return true;
    }();
    if (message) {
      vector<TString> races;
      for (auto& col : *allianceAttack)
        if (!races.contains(col->getName()->race))
          races.push_back(col->getName()->race);
      collective->addRecordedEvent(TSentence("THE_LAST_ALLIANCE_OF", combineWithAnd(races)));
      auto data = ScriptedUIDataElems::Record{};
      data.elems["message"] = TString(TSentence("TRIBES_HAVE_FORMED_ALLIANCE", races));
      data.elems["view_id"] = ViewIdList{allianceAttack->front()->getName()->viewId};
      getView()->scriptedUI("alliance_message", data);
      allianceAttack = none;
    }
  }
}

void PlayerControl::considerNewAttacks() {
  for (auto attack : copyOf(newAttacks))
    if (isConsideredAttacking(attack)) {
      auto attacker = attack.getAttacker();
      newAttacks.removeElement(attack);
      setScrollPos(attack.getCreatures()[0]->getPosition().plus(Vec2(0, 5)));
      getView()->refreshView();
      if (attack.getRansom() && collective->hasResource({ResourceId("GOLD"), *attack.getRansom()})) {
        if (getView()->yesOrNoPrompt(TSentence("DEMAND_GOLD",
            capitalFirst(attack.getAttackerName()), TString(*attack.getRansom())),
            attack.getAttackerViewId(), true)) {
          collective->takeResource({ResourceId("GOLD"), *attack.getRansom()});
          attacker->onRansomPaid();
          auto name = attacker->getName();
          getGame()->addAnalytics("ransom_paid", name ? name->full.data() : "unknown");
          break;
        }
      } else
        addWindowMessage(attack.getAttackerViewId(), TSentence("YOU_ARE_UNDER_ATTACK_BY", attack.getAttackerName()));
      getGame()->setCurrentMusic(MusicType::BATTLE);
      if (attacker)
        collective->addKnownVillain(attacker);
      notifiedAttacks.push_back(attack);
      break;
    }
}

void PlayerControl::considerSoloAchievement() {
  auto& fighters = collective->getCreatures(MinionTrait::FIGHTER);
  auto& leaders = collective->getLeaders();
  if (leaders.size() > 1 || (!fighters.empty() && (fighters.size() > 1 || !leaders.contains(fighters[0]))))
    soloKeeper = false;
  if (collective->getImmigration().getImmigrants().empty() && !collective->getConfig().canCapturePrisoners() &&
      leaders.size() == 1 && leaders[0]->getAttributes().getCreatureId() == CreatureId("CYCLOPS_PLAYER")) {
    soloKeeper = true;
    if (getGame()->gameWon())
      getGame()->achieve(AchievementId("solo_keeper"));
  }
}

void PlayerControl::tick() {
  prisonSizeCache.clear();
  collective->getConstructions().checkDebtConsistency();
  PROFILE_BLOCK("PlayerControl::tick");
  for (auto c : collective->getCreatures()) {
    if (c->isAffectedPermanently(BuffId("BRIDGE_BUILDING_SKILL")))
      c->removePermanentEffect(BuffId("BRIDGE_BUILDING_SKILL"), 1, false);
  }
  considerSoloAchievement();
  updateUnknownLocations();
  considerTransferingLostMinions();
  constexpr double messageTimeout = 80;
  for (auto& elem : messages)
    elem.setFreshness(max(0.0, elem.getFreshness() - 1.0 / messageTimeout));
  messages = messages.filter([&] (const PlayerMessage& msg) {
      return msg.getFreshness() > 0; });
  considerNightfallMessage();
  checkKeeperDanger();
  if (auto msg = collective->getWarnings().getNextWarning(getModel()->getLocalTime()))
    addMessage(PlayerMessage(*msg, MessagePriority::HIGH));
  considerNewAttacks();
  considerAllianceAttack();
  if (notifiedAttacks.empty())
    getGame()->setDefaultMusic();
  auto time = collective->getLocalTime();
  if (getGame()->getOptions()->getBoolValue(OptionId::HINTS) && time > hintFrequency) {
    int numHint = time.getDouble() / hintFrequency.getDouble() - 1;
    if (numHint < hints.size() && !hints[numHint].empty()) {
      addMessage(PlayerMessage(hints[numHint], MessagePriority::HIGH));
      hints[numHint] = TString();
    }
  }
}

bool PlayerControl::canSee(const Creature* c) const {
  return canSee(c->getPosition());
}

bool PlayerControl::canSee(Position pos) const {
  return getGame()->getOptions()->getBoolValue(OptionId::SHOW_MAP) || visibilityMap->isVisible(pos);
}

TribeId PlayerControl::getTribeId() const {
  return collective->getTribeId();
}

bool PlayerControl::isEnemy(const Creature* c) const {
  auto& leaders = collective->getLeaders();
  return c->getTribeId() != getTribeId() && !leaders.empty() && leaders[0]->isEnemy(c);
}

void PlayerControl::onConquered(Creature* victim, Creature* killer) {
  if (!victim->isPlayer()) {
    setScrollPos(victim->getPosition().plus(Vec2(0, 5)));
    getView()->updateView(this, false);
  }
  auto game = getGame();
  if (killer)
    if (auto& a = killer->getAttributes().killedByAchievement)
      game->achieve(*a);
  game->gameOver(victim, collective->getKills().getSize(), collective->getDangerLevel() + collective->getPoints());
  if (!collective->getTerritory().isEmpty())
    for (auto col : game->getCollectives())
      if (col != collective && col->getCreatures().contains(killer) && col->getConfig().xCanEnemyRetire() &&
          isOneOf(col->getVillainType(), VillainType::MAIN, VillainType::LESSER)) {
        game->setExitInfo(GameSaveType::RETIRED_SITE);
        leaveControl();
        collective->makeConqueredRetired(col); // this call invalidates this
        return;
      }
}

void PlayerControl::onMemberKilledOrStunned(Creature* victim, const Creature* killer) {
  if (victim->isPlayer() &&
      (!collective->hasTrait(victim, MinionTrait::LEADER) || collective->getCreatures(MinionTrait::LEADER).size() > 1))
    onControlledKilled(victim);
  visibilityMap->remove(victim);
  if (victim->isDead())
    battleSummary.minionsKilled.push_back(victim);
  else
    battleSummary.minionsCaptured.push_back(victim);
}

void PlayerControl::onMemberAdded(Creature* c) {
  updateMinionVisibility(c);
  auto team = getControlled();
  if (collective->hasTrait(c, MinionTrait::PRISONER) && !team.empty() &&
      team[0]->getPosition().isSameLevel(c->getPosition()))
    addToCurrentTeam(c);
}

Model* PlayerControl::getModel() const {
  return collective->getModel();
}

Game* PlayerControl::getGame() const {
  return collective->getGame();
}

View* PlayerControl::getView() const {
  return getGame()->getView();
}

void PlayerControl::addAttack(const CollectiveAttack& attack) {
  newAttacks.push_back(attack);
}

void PlayerControl::addAllianceAttack(vector<Collective*> attackers) {
  allianceAttack = attackers;
}

void PlayerControl::updateSquareMemory(Position pos) {
  // without this check there was a crash when leaders were empty, possibly on game over?
  if (!collective->getLeaders().empty()) {
    ViewIndex index;
    pos.getViewIndex(index, collective->getLeaders()[0]); // use the leader as a generic viewer
    memory->update(pos, index);
  }
}

void PlayerControl::onConstructed(Position pos, FurnitureType type) {
  addToMemory(pos);
  if (getGame()->getContentFactory()->furniture.getData(type).isEyeball())
    visibilityMap->updateEyeball(pos);
}

PController PlayerControl::createMinionController(Creature* c) {
  return ::getMinionController(c, memory, this, controlModeMessages, visibilityMap, unknownLocations, tutorial);
}

static void considerAddingKeeperFloor(Position pos) {
  if (NOTNULL(pos.getFurniture(FurnitureLayer::GROUND))->getViewObject()->id() == ViewId("floor")) {
    auto& obj = pos.modFurniture(FurnitureLayer::GROUND)->getViewObject();
    obj->setId(ViewId("keeper_floor"));
    obj->setDescription(TStringId("CLAIMED_FLOOR"));
  }
}

void PlayerControl::onClaimedSquare(Position position) {
  considerAddingKeeperFloor(position);
  position.setNeedsRenderAndMemoryUpdate(true);
  updateSquareMemory(position);
}

CollectiveControl::DuelAnswer PlayerControl::acceptDuel(Creature* attacker) {
  auto notified = [&] {
    for (auto& a : notifiedAttacks)
      if (a.getCreatures().contains(attacker))
        return true;
    return false;
  }();
  if (!notified)
    return DuelAnswer::UNKNOWN;
  if (getView()->yesOrNoPrompt(capitalFirst(TSentence("CHALLENGES_YOU_TO_A_DUEL", attacker->getName().aOrTitle())))) {
    controlSingle(collective->getLeaders()[0]);
    return DuelAnswer::ACCEPT;
  }
  return DuelAnswer::REJECT;
}

void PlayerControl::onDestructed(Position pos, FurnitureType type, const DestroyAction& action) {
  if (action.getType() == DestroyAction::Type::DIG) {
    Vec2 visRadius(3, 3);
    for (Position v : pos.getRectangle(Rectangle(-visRadius, visRadius + Vec2(1, 1)))) {
      collective->addKnownTile(v);
      updateSquareMemory(v);
    }
    considerAddingKeeperFloor(pos);
    pos.setNeedsRenderAndMemoryUpdate(true);
  }
  auto game = getGame();
  if (auto& a = game->getContentFactory()->furniture.getData(type).getMinedAchievement())
    game->achieve(*a);
}

vector<Vec2> PlayerControl::getVisibleEnemies() const {
  return {};
}

TribeAlignment PlayerControl::getTribeAlignment() const {
  return tribeAlignment;
}

REGISTER_TYPE(ListenerTemplate<PlayerControl>)
