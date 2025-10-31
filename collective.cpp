#include "stdafx.h"
#include "collective.h"
#include "collective_control.h"
#include "creature.h"
#include "effect.h"
#include "level.h"
#include "item.h"
#include "item_factory.h"
#include "statistics.h"
#include "technology.h"
#include "monster.h"
#include "options.h"
#include "model.h"
#include "game.h"
#include "spell.h"
#include "view_id.h"
#include "equipment.h"
#include "view_index.h"
#include "minion_equipment.h"
#include "task_map.h"
#include "collective_teams.h"
#include "known_tiles.h"
#include "construction_map.h"
#include "minion_activity_map.h"
#include "tribe.h"
#include "collective_config.h"
#include "creature_name.h"
#include "cost_info.h"
#include "monster_ai.h"
#include "task.h"
#include "territory.h"
#include "collective_attack.h"
#include "gender.h"
#include "collective_name.h"
#include "creature_attributes.h"
#include "villain_type.h"
#include "workshops.h"
#include "attack_trigger.h"
#include "spell_map.h"
#include "body.h"
#include "furniture.h"
#include "furniture_factory.h"
#include "zones.h"
#include "furniture_usage.h"
#include "collective_warning.h"
#include "immigration.h"
#include "creature_factory.h"
#include "resource_info.h"
#include "workshop_item.h"
#include "position_matching.h"
#include "storage_id.h"
#include "game_config.h"
#include "conquer_condition.h"
#include "game_event.h"
#include "view_object.h"
#include "content_factory.h"
#include "effect_type.h"
#include "immigrant_info.h"
#include "item_types.h"
#include "health_type.h"
#include "village_control.h"
#include "automaton_part.h"
#include "enemy_aggression_level.h"
#include "furnace.h"
#include "promotion_info.h"
#include "dancing.h"
#include "assembled_minion.h"
#include "creature_experience_info.h"

template <class Archive>
void Collective::serialize(Archive& ar, const unsigned int version) {
  if (Archive::is_saving::value)
    CHECK(!model->serializationLocked);
  ar(SUBCLASS(TaskCallback), SUBCLASS(UniqueEntity<Collective>), SUBCLASS(EventListener));
  ar(creatures, taskMap, tribe, control, byTrait, populationGroups, hadALeader, dancing, lockedEquipmentGroups);
  ar(territory, alarmInfo, markedItems, constructions, minionEquipment, groupLockedAcitivities);
  ar(delayedPos, knownTiles, technology, kills, points, currentActivity, recordedEvents, allRecordedEvents);
  ar(credit, model, immigration, teams, name, minionActivities, attackedByPlayer, furnace, stunnedMinions);
  ar(config, warnings, knownVillains, knownVillainLocations, banished, positionMatching, steedAssignments);
  ar(villainType, enemyId, workshops, zones, discoverable, populationIncrease, dungeonLevel);
}

SERIALIZABLE(Collective)

SERIALIZATION_CONSTRUCTOR_IMPL(Collective)

Collective::Collective(Private, Model* model, TribeId t, const optional<CollectiveName>& n,
    const ContentFactory* contentFactory)
    : positionMatching(makeOwner<PositionMatching>()), tribe(t), model(NOTNULL(model)), name(to_heap_optional(n)),
      villainType(VillainType::NONE), minionActivities(contentFactory) {
}

PCollective Collective::create(Model* model, TribeId tribe, const optional<CollectiveName>& name, bool discoverable,
    const ContentFactory* contentFactory) {
  auto ret = makeOwner<Collective>(Private {}, model, tribe, name, contentFactory);
  ret->subscribeTo(model);
  ret->discoverable = discoverable;
  ret->workshops = make_unique<Workshops>(WorkshopArray(), contentFactory);
  ret->immigration = makeOwner<Immigration>(ret.get(), vector<ImmigrantInfo>());
  ret->dancing = make_unique<Dancing>(contentFactory);
  return ret;
}

void Collective::init(CollectiveConfig cfg) {
  config.reset(std::move(cfg));
}

void Collective::setImmigration(PImmigration i) {
  immigration = std::move(i);
}

void Collective::setWorkshops(unique_ptr<Workshops> w) {
  workshops = std::move(w);
}

const heap_optional<CollectiveName>& Collective::getName() const {
  return name;
}

void Collective::setVillainType(VillainType t) {
  villainType = t;
}

bool Collective::isDiscoverable() const {
  return discoverable;
}

void Collective::setEnemyId(EnemyId id) {
  enemyId = id;
}

VillainType Collective::getVillainType() const {
  return villainType;
}

optional<EnemyId> Collective::getEnemyId() const {
  return enemyId;
}

Collective::~Collective() {
}

void Collective::setPopulationGroup(const vector<Creature*>& creatures) {
  for (auto c : copyOf(populationGroups))
    if (c.size() == 1 && creatures.contains(c[0]))
      populationGroups.removeElement(c);
  populationGroups.push_back(creatures);
}

Creature* Collective::addCreature(PCreature creature, Position pos, EnumSet<MinionTrait> traits) {
  Creature* c = creature.get();
  if (pos.landCreature(std::move(creature))) {
    addCreature(c, traits);
    return c;
  }
  return nullptr;
}

void Collective::updateCreatureStatus(Creature* c) {
  c->getStatus().set(CreatureStatus::CIVILIAN,
      getCreatures().contains(c) &&
      c->getBody().isHumanoid() &&
      !c->isAffected(LastingEffect::STUNNED) &&
      !hasTrait(c, MinionTrait::FIGHTER) &&
      !hasTrait(c, MinionTrait::LEADER));
  c->getStatus().set(CreatureStatus::FIGHTER, hasTrait(c, MinionTrait::FIGHTER));
  c->getStatus().set(CreatureStatus::LEADER, hasTrait(c, MinionTrait::LEADER));
  c->getStatus().set(CreatureStatus::PRISONER, hasTrait(c, MinionTrait::PRISONER));
}

void Collective::takePrisoner(Creature* c) {
  c->removeEffect(LastingEffect::STUNNED);
  c->unbindPhylactery();
  addCreature(c, {MinionTrait::WORKER, MinionTrait::PRISONER, MinionTrait::NO_LIMIT});
}

void Collective::addCreature(Creature* c, EnumSet<MinionTrait> traits) {
  CHECK(!c->isDead());
  CHECK(!creatures.contains(c)) << "Creature already added " << c->identify();
  if (c->getGlobalTime()) { // only do this if creature already exists on the map
    c->addEffect(LastingEffect::RESTED, 500_visible, false);
    c->addEffect(LastingEffect::SATIATED, 500_visible, false);
  }
  if (c->isAffected(LastingEffect::SUMMONED)) {
    traits.insert(MinionTrait::NO_LIMIT);
    traits.insert(MinionTrait::SUMMONED);
  }
  if (!traits.contains(MinionTrait::FARM_ANIMAL) && !c->getController()->dontReplaceInCollective())
    c->setController(makeOwner<Monster>(c, MonsterAIFactory::collective(this)));
  if (traits.contains(MinionTrait::LEADER)) {
    hadALeader = true;
    if (config->isLeaderFighter())
      traits.insert(MinionTrait::FIGHTER);
  }
  if (c->getTribeId() != *tribe)
    c->setTribe(*tribe);
  if (auto game = getGame())
    for (Collective* col : getGame()->getCollectives())
      if (col->getCreatures().contains(c))
        col->removeCreature(c);
  creatures.push_back(c);
  populationGroups.push_back({c});
  for (MinionTrait t : traits)
    byTrait[t].push_back(c);
  updateCreatureStatus(c);
  if (!traits.contains(MinionTrait::PRISONER))
    for (Item* item : c->getEquipment().getItems())
      minionEquipment->tryToOwn(c, item);
  for (auto minion : getCreatures()) {
    c->removePrivateEnemy(minion);
    minion->removePrivateEnemy(c);
  }
  control->onMemberAdded(c);
}

void Collective::removeCreature(Creature* c) {
  creatures.removeElement(c);
  setSteed(c, nullptr);
  for (auto& group : populationGroups)
    group.removeElementMaybe(c);
  for (auto& group : copyOf(populationGroups))
    if (group.size() == 0)
      populationGroups.removeElement(group);
  returnResource(taskMap->freeFromTask(c));
  for (auto team : teams->getContaining(c))
    teams->remove(team, c);
  for (MinionTrait t : ENUM_ALL(MinionTrait))
    byTrait[t].removeElementMaybePreserveOrder(c);
  updateCreatureStatus(c);
}

void Collective::banishCreature(Creature* c) {
  removeCreature(c);
  if (c->isAutomaton()) {
    taskMap->addTask(Task::disassemble(c), c->getPosition(), MinionActivity::CRAFT);
  } else {
    vector<Position> exitTiles = territory->getExtended(10, 20);
    vector<PTask> tasks;
    vector<Item*> items = c->getEquipment().getItems();
    if (!items.empty())
      tasks.push_back(Task::dropItemsAnywhere(items));
    if (!exitTiles.empty())
      tasks.push_back(Task::goToTryForever(Random.choose(exitTiles)));
    tasks.push_back(Task::disappear());
    c->setController(makeOwner<Monster>(c, MonsterAIFactory::singleTask(Task::chain(std::move(tasks)))));
  }
  banished.insert(c);
}

bool Collective::wasBanished(const Creature* c) const {
  return banished.contains(c);
}

/*vector<Creature*> Collective::getRecruits() const {
  vector<Creature*> ret;
  vector<Creature*> possibleRecruits = filter(getCreatures(MinionTrait::FIGHTER),
      [] (const Creature* c) { return c->getAttributes().getRecruitmentCost() > 0; });
  if (auto minPop = config->getRecruitingMinPopulation())
    for (int i = *minPop; i < possibleRecruits.size(); ++i)
      ret.push_back(possibleRecruits[i]);
  return ret;
}*/

bool Collective::hasTradeItems() const {
  for (Position pos : territory->getAll())
    if (!pos.getItems(ItemIndex::FOR_SALE).empty())
      return true;
  return false;
}

//kocham Cię

vector<Item*> Collective::getTradeItems() const {
  vector<Item*> ret;
  for (Position pos : territory->getAll())
    append(ret, pos.getItems(ItemIndex::FOR_SALE));
  return ret;
}

PItem Collective::buyItem(Item* item) {
  for (Position pos : territory->getAll())
    for (Item* it : pos.getItems(ItemIndex::FOR_SALE))
      if (it == item) {
        PItem ret = pos.removeItem(it);
        ret->setShopkeeper(nullptr);
        return ret;
      }
  FATAL << "Couldn't find item";
  return nullptr;
}

vector<TriggerInfo> Collective::getTriggers(const Collective* against) const {
  if (!isConquered())
    return control->getTriggers(against);
  else
    return {};
}

const vector<Creature*>& Collective::getLeaders() const {
  return byTrait[MinionTrait::LEADER];
}

Game* Collective::getGame() const {
  return model->getGame();
}

CollectiveControl* Collective::getControl() const {
  return control.get();
}

TribeId Collective::getTribeId() const {
  return *tribe;
}

Tribe* Collective::getTribe() const {
  return getGame()->getTribe(*tribe);
}

Model* Collective::getModel() const {
  return model;
}

const vector<Creature*>& Collective::getCreatures() const {
  return creatures;
}

void Collective::setMinionActivity(Creature* c, MinionActivity activity) {
  auto current = getCurrentActivity(c);
  if (current.activity != activity) {
    freeFromTask(c);
    c->removeEffect(LastingEffect::SLEEP);
    currentActivity.set(c, {activity, getLocalTime() +
        MinionActivities::getDuration(c, activity).value_or(-1_visible)});
  }
}

Collective::CurrentActivity Collective::getCurrentActivity(const Creature* c) const {
  return currentActivity.getMaybe(c)
      .value_or(CurrentActivity{MinionActivity::IDLE, getLocalTime() - 1_visible});
}

TString Collective::getMinionGroupName(const Creature* c) const {
  if (hasTrait(c, MinionTrait::PRISONER)) {
    return TStringId("PRISONER");
  } else
    return c->getName().stack();
}

vector<TString> Collective::getAutomatonGroupNames(const Creature* c) const {
  vector<TString> ret;
  for (auto& part : c->getAutomatonParts())
    if (!part.minionGroup.empty() && !ret.contains(part.minionGroup))
      ret.push_back(part.minionGroup);
  for (auto& part : c->getPromotions())
    if (!ret.contains(part.name))
      ret.push_back(part.name);
  return ret;
}

bool Collective::isActivityGoodAssumingHaveTasks(Creature* c, MinionActivity activity, bool ignoreTaskLock) {
  PROFILE;
  if (!c->getAttributes().getMinionActivities().isAvailable(this, c, activity, ignoreTaskLock))
    return false;
  switch (activity) {
    case MinionActivity::BE_WHIPPED:
      return !c->isAffected(BuffId("HIGH_MORALE"));
    case MinionActivity::CROPS:
    case MinionActivity::EXPLORE:
      return getGame()->getSunlightInfo().getState() == SunlightState::DAY;
    case MinionActivity::SLEEP:
      if (!config->hasVillainSleepingTask())
        return true;
      FALLTHROUGH;
    case MinionActivity::EXPLORE_NOCTURNAL:
      return getGame()->getSunlightInfo().getState() == SunlightState::NIGHT;
    case MinionActivity::BE_TORTURED:
      return getMaxPopulation() > getPopulationSize();
    default: return true;
  }
}

bool Collective::isActivityGroupLocked(const Creature* c, MinionActivity activity) const {
  auto ret = isActivityGroupLocked(getMinionGroupName(c), activity);
  if (MinionActivityMap::isActivityAutoGroupLocked(activity))
    for (auto& group : getAutomatonGroupNames(c))
      ret = ret && isActivityGroupLocked(group, activity);
  else
    for (auto& group : getAutomatonGroupNames(c))
      ret = ret || isActivityGroupLocked(group, activity);
  return ret;
}

bool Collective::isActivityGroupLocked(const TString& group, MinionActivity activity) const {
  auto ret = MinionActivityMap::isActivityAutoGroupLocked(activity);
  if (auto res = getValueMaybe(groupLockedAcitivities, group))
    ret ^= res->contains(activity);
  return ret;
}

void Collective::flipGroupLockedActivities(const TString& group, GroupLockedActivities a) {
  if (auto v = getReferenceMaybe(groupLockedAcitivities, group))
    *v = v->ex_or(a);
  else
    groupLockedAcitivities.insert(make_pair(group, a));
}

bool Collective::isActivityGood(Creature* c, MinionActivity activity, bool ignoreTaskLock) {
  PROFILE;
  return isActivityGoodAssumingHaveTasks(c, activity, ignoreTaskLock) &&
      (minionActivities->generate(this, c, activity) || MinionActivities::getExisting(this, c, activity));
}

bool Collective::isConquered() const {
  return config->isConquered(this);
}

void Collective::setPriorityTask(Creature* c, PTask task) {
  returnResource(taskMap->freeFromTask(c));
  taskMap->setPriorityTask(taskMap->addTaskFor(std::move(task), c));
}

void Collective::setTask(Creature* c, PTask task) {
  returnResource(taskMap->freeFromTask(c));
  taskMap->addTaskFor(std::move(task), c);
}

bool Collective::hasTask(const Creature* c) const {
  return taskMap->hasTask(c);
}

void Collective::freeFromTask(const Creature* c) {
  taskMap->freeFromTask(c);
}

void Collective::setControl(PCollectiveControl c) {
  control = std::move(c);
}

void Collective::makeConqueredRetired(Collective* conqueror) {
  auto control = VillageControl::copyOf(this, dynamic_cast<VillageControl*>(conqueror->control.get()));
  control->updateAggression(EnemyAggressionLevel::NONE);
  setControl(std::move(control));
  getGame()->clearPlayerControl();
  name = conqueror->name;
  config = conqueror->config;
  discoverable = conqueror->discoverable;
  immigration = makeOwner<Immigration>(this, conqueror->immigration->getImmigrants());
  setVillainType(conqueror->getVillainType());
  setEnemyId(*conqueror->getEnemyId());
  auto oldCreatures = getCreatures();
  for (auto c : copyOf(conqueror->getCreatures()))
    // shopkeepers keep references to their shop positions so hack it by skipping them
    if (c->getAttributes().getCreatureId() != CreatureId("SHOPKEEPER"))
      addCreature(c, conqueror->getAllTraits(c));
  for (auto c : copyOf(creatures)) // creatures might die during transfer so make a copy
    getGame()->transferCreature(c, getModel(), territory->getAll());
  for (auto c : oldCreatures)
    c->dieNoReason();
  if (getLeaders().empty()) {
    auto fighters = getCreatures(MinionTrait::FIGHTER);
    if (!fighters.empty())
      setTrait(Random.choose(fighters), MinionTrait::LEADER);
    else {
      CHECK(!getCreatures().empty());
      setTrait(Random.choose(getCreatures()), MinionTrait::LEADER);
    }
  }
}

vector<Position> Collective::getEnemyPositions() const {
  PROFILE;
  vector<Position> enemyPos;
  for (Position pos : territory->getExtended(10))
    if (const Creature* c = pos.getCreature())
      if (getTribe()->isEnemy(c) && !c->isAffected(LastingEffect::STUNNED))
        enemyPos.push_back(pos);
  return enemyPos;
}

void Collective::addNewCreatureMessage(const vector<Creature*>& immigrants) {
  if (immigrants.size() == 1)
    control->addMessage(PlayerMessage(TSentence("JOINS_YOUR_FORCES", immigrants[0]->getName().a()))
        .setCreature(immigrants[0]->getUniqueId()));
  else {
    control->addMessage(PlayerMessage(TSentence("GROUP_JOINS_YOUR_FORCES", {
        immigrants[0]->getName().getGroupName(),
        TString(immigrants.size()),
        immigrants[0]->getName().plural()}))
        .setCreature(immigrants[0]->getUniqueId()));
  }
}

static int getKeeperUpgradeLevel(int dungeonLevel) {
  return (dungeonLevel + 1) / 5;
}

void Collective::updateTeamExperience() {
  for (auto c : getCreatures())
    c->setTeamExperience(0, false);
  for (TeamId team : getTeams().getAllActive()) {
    double exp = 0;
    bool leadership = false;
    for (auto c : getTeams().getMembers(team))
      if (c->isAffected(BuffId("LEADERSHIP"))) {
        exp = c->getCombatExperience(true, false);
        leadership = true;
        break;
      } else
        exp = max(exp, c->getCombatExperience(true, false));
    for (auto c : getTeams().getMembers(team))
      c->setTeamExperience(exp, leadership);
  }
}

void Collective::update(bool currentlyActive) {
  updateTeamExperience();
  for (auto leader : getLeaders()) {
    leader->upgradeViewId(getKeeperUpgradeLevel(dungeonLevel.level));
    name->viewId = leader->getViewIdWithWeapon();
  }
  control->update(currentlyActive);
  if (config->hasImmigrantion(currentlyActive) && (!getLeaders().empty() || !hadALeader) && !isConquered())
    immigration->update();
}

double Collective::getRebellionProbability() const {
  const double allowedPrisonerRatio = 0.5;
  const double maxPrisonerRatio = 1.5;
  const int numPrisoners = getCreatures(MinionTrait::PRISONER).size();
  const int numFighters = getCreatures(MinionTrait::FIGHTER).size();
  const int numFreePrisoners = 4;
  if (numPrisoners <= numFreePrisoners)
    return 0;
  if (numFighters == 0)
    return 1;
  double ratio = double(numPrisoners - numFreePrisoners) / double(numFighters);
  return min(1.0, max(0.0, (ratio - allowedPrisonerRatio) / (maxPrisonerRatio - allowedPrisonerRatio)));
}

void Collective::considerRebellion() {
  if (getGame()->getPlayerCollective() == this && Random.chance(getRebellionProbability() / 1000)) {
    Position escapeTarget = model->getGroundLevel()->getLandingSquare(StairKey::transferLanding(),
        Random.choose(Vec2::directions8()));
    for (auto c : copyOf(getCreatures(MinionTrait::PRISONER))) {
      removeCreature(c);
      c->setController(makeOwner<Monster>(c, MonsterAIFactory::singleTask(
          Task::chain(Task::goToTryForever(escapeTarget), Task::disappear()))));
      c->setTribe(TribeId::getMonster());
      c->removeEffect(LastingEffect::SLEEP);
      c->removeEffect(LastingEffect::ENTANGLED);
      c->removeEffect(LastingEffect::TIED_UP);
      c->toggleCaptureOrder();
    }
    control->addMessage(PlayerMessage(TStringId("PRISONERS_ESCAPING"), MessagePriority::CRITICAL));
  }
}

void Collective::updateBorderTiles() {
  if (!updatedBorderTiles) {
    knownTiles->limitBorderTiles(getModel());
    updatedBorderTiles = true;
  }
}

void Collective::updateGuardTasks() {
  bool setWarning = false;
  for (auto activity : {make_pair(MinionActivity::GUARDING1, ZoneId::GUARD1),
      make_pair(MinionActivity::GUARDING2, ZoneId::GUARD2),
      make_pair(MinionActivity::GUARDING3, ZoneId::GUARD3)}) {
    for (auto& pos : zones->getPositions(activity.second))
      if (!taskMap->hasTask(pos, activity.first))
        taskMap->addTask(Task::goToAndWait(pos, 400_visible), pos, activity.first);
    for (auto& task : taskMap->getTasks(activity.first))
      if (auto pos = taskMap->getPosition(task))
        if (!zones->getPositions(activity.second).count(*pos))
          taskMap->removeTask(task);
    setWarning = setWarning || (!zones->getPositions(activity.second).empty() &&
      [&] {
        for (auto c : getCreatures(MinionTrait::FIGHTER))
          if (c->getAttributes().getMinionActivities().isAvailable(this, c, activity.first, false))
            return false;
        return true;
      }());
  }
  warnings->setWarning(CollectiveWarning::GUARD_POSTS, setWarning);
}

void Collective::updateAutomatonEngines() {
  auto totalPop = getMaxPopulation();
  for (auto c : getCreatures(MinionTrait::AUTOMATON)) {
    bool off = c->getAttributes().isAffectedPermanently(LastingEffect::TURNED_OFF);
    if (totalPop > 0) {
      if (off)
        c->removePermanentEffect(LastingEffect::TURNED_OFF);
    } else
      if (!off)
        c->addPermanentEffect(LastingEffect::TURNED_OFF);
    --totalPop;
    if (c->getAttributes().isAffectedPermanently(LastingEffect::TURNED_OFF))
      taskMap->freeFromTask(c);
  }
}

bool Collective::minionCanUseQuarters(Creature* c) {
  return hasTrait(c, MinionTrait::FIGHTER) || hasTrait(c, MinionTrait::LEADER);
}

bool Collective::minionHasQuarters(UniqueEntity<Creature>::Id id) {
  return !getZones().getQuarters(id).empty();
}

void Collective::updateMinionPromotions() {
  if (!config->minionsRequireQuarters())
    return;
  for (auto c : getCreatures())
    if (minionCanUseQuarters(c))
      c->setMaxPromotion(getMaxPromotionLevel(zones->getQuartersLuxury(c->getUniqueId()).value_or(-1)));
}

void Collective::tick() {
  PROFILE_BLOCK("Collective::tick");
  updateBorderTiles();
  considerRebellion();
  updateGuardTasks();
  updateMinionPromotions();
  dangerLevelCache = none;
  control->tick();
  zones->tick();
  taskMap->tick();
  constructions->clearUnsupportedFurniturePlans();
  dancing->setArea(zones->getPositions(ZoneId::LEISURE), getModel()->getLocalTime());
  if (config->getWarnings() && Random.roll(5))
    warnings->considerWarnings(this);
  if (config->getEnemyPositions() && Random.roll(5)) {
    vector<Position> enemyPos = getEnemyPositions();
    if (!enemyPos.empty())
      delayDangerousTasks(enemyPos, getLocalTime() + 20_visible);
    else
      alarmInfo.reset();
  }
  if (config->getConstructions())
    updateConstructions();
  for (auto& workshop : workshops->types)
    workshop.second.updateState(this);
  if (Random.roll(5)) {
    for (Position pos : territory->getAll())
      if (!isDelayed(pos) && pos.canEnterEmpty(MovementTrait::WALK) && !pos.getItems().empty())
        fetchItems(pos);
    for (Position pos : zones->getPositions(ZoneId::FETCH_ITEMS))
      if (!isDelayed(pos) && pos.canEnterEmpty(MovementTrait::WALK))
        fetchItems(pos);
    for (Position pos : zones->getPositions(ZoneId::PERMANENT_FETCH_ITEMS))
      if (!isDelayed(pos) && pos.canEnterEmpty(MovementTrait::WALK))
        fetchItems(pos);
  }

  if (config->getManageEquipment() && Random.roll(40)) {
    minionEquipment->updateOwners(getCreatures());
    minionEquipment->updateItems(getAllItems(ItemIndex::MINION_EQUIPMENT, true));
    for (auto c : getCreatures())
      for (auto it : minionEquipment->getItemsOwnedBy(c))
        if (auto& group = it->getEquipmentGroup())
          if (!canUseEquipmentGroup(c, *group))
            minionEquipment->discard(it);
  }
  updateAutomatonEngines();
  autoAssignSteeds();
}

void Collective::autoAssignSteeds() {
  for (auto c : getCreatures())
    if (!canUseEquipmentGroup(c, TStringId("STEEDS_EQUIPMENT_GROUP")))
      setSteed(c, nullptr);
  vector<Creature*> freeSteeds = getCreatures().filter(
      [this] (auto c) { return c->isAffected(LastingEffect::STEED) && !getSteedOrRider(c);});
  auto factory = getGame()->getContentFactory();
  sort(freeSteeds.begin(), freeSteeds.end(), [&](auto c1, auto c2) {
      return c1->getBestAttack(factory).value < c2->getBestAttack(factory).value; });
  auto minions = getCreatures().filter([this] (auto c) {
    return c->isAffected(LastingEffect::RIDER) && !getSteedOrRider(c) &&
        canUseEquipmentGroup(c, TStringId("STEEDS_EQUIPMENT_GROUP"));
  });
  sort(minions.begin(), minions.end(), [&](auto c1, auto c2) {
      auto leader1 = hasTrait(c1, MinionTrait::LEADER);
      auto leader2 = hasTrait(c2, MinionTrait::LEADER);
      return (leader1 && !leader2) || (leader1 == leader2 &&
          c1->getBestAttack(factory).value > c2->getBestAttack(factory).value); });
  for (auto c : minions) {
    auto steed = getSteedOrRider(c);
    auto bestAvailable = [&]() -> Creature* {
      for (int i : All(freeSteeds).reverse())
        if (c->isSteedGoodSize(freeSteeds[i]))
          return freeSteeds[i];
      return nullptr;
    }();
    auto companion = c->getSteedCompanion();
    if (!!companion &&
        (!bestAvailable || companion->getBestAttack(factory).value > bestAvailable->getBestAttack(factory).value))
      bestAvailable = companion;
    if (bestAvailable &&
        (!steed || steed->getBestAttack(factory).value < bestAvailable->getBestAttack(factory).value)) {
      setSteed(c, bestAvailable);
      freeSteeds.removeElementMaybePreserveOrder(bestAvailable);
    }
  }
}

const vector<Creature*>& Collective::getCreatures(MinionTrait trait) const {
  return byTrait[trait];
}

EnumSet<MinionTrait> Collective::getAllTraits(Creature* c) const {
  CHECK(creatures.contains(c));
  EnumSet<MinionTrait> ret;
  for (auto t : ENUM_ALL(MinionTrait))
    if (hasTrait(c, t))
      ret.insert(t);
  return ret;
}

bool Collective::hasTrait(const Creature* c, MinionTrait t) const {
  return byTrait[t].contains(c);
}

bool Collective::setTrait(Creature* c, MinionTrait t) {
  if (!hasTrait(c, t)) {
    byTrait[t].push_back(c);
    updateCreatureStatus(c);
    return true;
  }
  return false;
}

bool Collective::removeTrait(Creature* c, MinionTrait t) {
  if (byTrait[t].removeElementMaybePreserveOrder(c)) {
    updateCreatureStatus(c);
    return true;
  }
  return false;
}


void Collective::addMoraleForKill(Creature* killer, Creature* victim) {
  killer->addEffect(BuffId("HIGH_MORALE"), 30_visible);
}

void Collective::decreaseMoraleForKill(Creature* killer, Creature* victim) {
/*  if (hasTrait(victim, MinionTrait::LEADER))
    for (Creature* c : getCreatures())
      c->addPermanentEffect(BuffId("LOW_MORALE"));*/
}

const optional<Collective::AlarmInfo>& Collective::getAlarmInfo() const {
  return alarmInfo;
}

bool Collective::needsToBeKilledToConquer(const Creature* c) const {
  return (hasTrait(c, MinionTrait::FIGHTER) && !c->wasDuel()) || hasTrait(c, MinionTrait::LEADER);
}

bool Collective::creatureConsideredPlayer(Creature* c) const {
  return c->isPlayer() ||
    (!getGame()->getPlayerCreatures().empty() && getGame()->getPlayerCreatures()[0]->getTribe() == c->getTribe()) ||
    (getGame()->getPlayerCollective() && getGame()->getPlayerCollective()->getCreatures().contains(c));
}

void Collective::onEvent(const GameEvent& event) {
  PROFILE;
  using namespace EventInfo;
  event.visit<void>(
      [&](const Alarm& info) {
        static const auto alarmTime = 100_visible;
        if (getTerritory().contains(info.pos)) {
          if (!info.silent)
            control->addMessage(PlayerMessage(TStringId("ALARM_GOES_OFF"), MessagePriority::HIGH).setPosition(info.pos));
          alarmInfo = AlarmInfo {getGlobalTime() + alarmTime, info.pos };
          for (Creature* c : byTrait[MinionTrait::FIGHTER])
            if (c->isAffected(LastingEffect::SLEEP))
              c->removeEffect(LastingEffect::SLEEP);
        }
      },
      [&](const CreatureKilled& info) {
        if (getCreatures().contains(info.victim))
          onMinionKilled(info.victim, info.attacker);
        if (getCreatures().contains(info.attacker))
          onKilledSomeone(info.attacker, info.victim);
      },
      [&](const CreatureTortured& info) {
        auto victim = info.victim;
        if (getCreatures().contains(victim)) {
          if (Random.roll(30)) {
            addRecordedEvent(TSentence("THE_TORTURING_OF", victim->getName().aOrTitle()));
            if (victim->getUniqueId().getGenericId() % 2 == 0) {
              victim->dieWithReason(TStringId("KILLED_BY_TORTURE"));
            } else {
              control->addMessage(TSentence("IS_CONVERTED", victim->getName().a()));
              removeTrait(victim, MinionTrait::PRISONER);
              removeTrait(victim, MinionTrait::WORKER);
              removeTrait(victim, MinionTrait::NO_LIMIT);
              setTrait(victim, MinionTrait::FIGHTER);
              victim->removeEffect(LastingEffect::TIED_UP);
              setMinionActivity(victim, MinionActivity::IDLE);
              getGame()->achieve(AchievementId("converted_prisoner"));
            }
          }
        }
      },
      [&](const CreatureStunned& info) {
        auto victim = info.victim;
        addRecordedEvent(TSentence("THE_CAPTURING_OF", victim->getName().aOrTitle()));
        if (getCreatures().contains(victim)) {
          stunnedMinions.set(victim, getAllTraits(info.victim));
          control->addMessage(PlayerMessage(TSentence("HAS_BEEN_CAPTURED_BY_ENEMY", info.victim->getName().aOrTitle()),
              MessagePriority::HIGH));
          if (info.attacker && creatureConsideredPlayer(info.attacker))
            attackedByPlayer = true;
          bool fighterStunned = needsToBeKilledToConquer(victim);
          removeTrait(victim, MinionTrait::FIGHTER);
          removeTrait(victim, MinionTrait::LEADER);
          control->addMessage(PlayerMessage(TSentence("IS_UNCONCIOUS", victim->getName().a()))
              .setPosition(victim->getPosition()));
          control->onMemberKilledOrStunned(info.victim, info.attacker);
          for (auto team : teams->getContaining(victim))
            teams->remove(team, victim);
          setSteed(victim, nullptr);
          freeFromTask(victim);
          if (isConquered() && fighterStunned)
            getGame()->addEvent(EventInfo::ConqueredEnemy{this, attackedByPlayer});
        }
      },
      [&](const TrapDisarmed& info) {
        control->addMessage(PlayerMessage(TSentence("DISARMS", info.creature->getName().a(),
            getGame()->getContentFactory()->furniture.getData(info.type).getName()),
            MessagePriority::HIGH).setPosition(info.pos));
      },
      [&](const MovementChanged& info) {
        positionMatching->updateMovement(info.pos);
      },
      [&](const FurnitureRemoved& info) {
        auto curFur = info.position.getFurniture(info.layer);
        if (info.position.getModel() == model && (!curFur || !!strcmp(curFur->getType().data(), "PHYLACTERY_ACTIVE")) &&
            constructions->containsFurniture(info.position, info.layer)) {
          constructions->onFurnitureDestroyed(info.position, info.layer, info.type);
          recalculateFurniturePopIncrease();
        }
      },
      [&](const auto&) {}
  );
}

void Collective::onMinionKilled(Creature* victim, Creature* killer) {
  if (killer)
    addRecordedEvent(TSentence("THE_SLAYING_OF", victim->getName().aOrTitle(), killer->getName().a()));
  else
    addRecordedEvent(TSentence("THE_DEATH_OF", victim->getName().aOrTitle()));
  if (killer && creatureConsideredPlayer(killer))
    attackedByPlayer = true;
  auto factory = getGame()->getContentFactory();
  control->onMemberKilledOrStunned(victim, killer);
  if (hasTrait(victim, MinionTrait::PRISONER) && killer && getCreatures().contains(killer))
    returnResource({ResourceId("PRISONER_HEAD"), 1});
  if (!hasTrait(victim, MinionTrait::FARM_ANIMAL) && !hasTrait(victim, MinionTrait::SUMMONED)) {
    auto& deathDescription = victim->getAttributes().getDeathDescription(factory);
    decreaseMoraleForKill(killer, victim);
    if (killer)
      control->addMessage(PlayerMessage(TSentence("IS_KILLED_BY", {
          victim->getName().a(), deathDescription, killer->getName().a()} ),
            MessagePriority::HIGH).setPosition(victim->getPosition()));
    else
      control->addMessage(PlayerMessage(TSentence("IS_KILLED",
          victim->getName().a(), deathDescription),
            MessagePriority::HIGH).setPosition(victim->getPosition()));
  }
  if (hasTrait(victim, MinionTrait::FIGHTER) || hasTrait(victim, MinionTrait::LEADER))
    for (auto other : getCreatures(MinionTrait::FIGHTER))
      if (other != victim && other->canSee(victim))
        LastingEffects::onAllyKilled(other);
  bool fighterKilled = needsToBeKilledToConquer(victim);
  removeCreature(victim);
  if (isConquered() && fighterKilled) {
    control->onConquered(victim, killer);
    getGame()->addEvent(EventInfo::ConqueredEnemy{this, attackedByPlayer});
  }
  if (auto& guardianInfo = getConfig().getGuardianInfo())
    if (Random.chance(guardianInfo->probability)) {
      auto& extended = territory->getStandardExtended();
      if (!extended.empty())
        Random.choose(extended).landCreature(factory->getCreatures().fromId(guardianInfo->creature, getTribeId()));
    }
}

void Collective::onKilledSomeone(Creature* killer, Creature* victim) {
  auto& deathDescription = victim->getAttributes().getDeathDescription(getGame()->getContentFactory());
  if (victim->getTribe() != getTribe()) {
    if (victim->getStatus().contains(CreatureStatus::LEADER))
      addRecordedEvent(TSentence("THE_SLAYING_OF", victim->getName().aOrTitle(), killer->getName().a()));
    addMoraleForKill(killer, victim);
    kills.insert(victim);
    int difficulty = victim->getDifficultyPoints();
//    CHECK(difficulty >=0 && difficulty < 100000) << difficulty << " " << victim->getName().bare();
    points += difficulty;
    control->addMessage(PlayerMessage(TSentence("IS_KILLED_BY", {
          victim->getName().a(), deathDescription, killer->getName().a()} ))
        .setPosition(victim->getPosition()));
    if (victim->getStatus().contains(CreatureStatus::CIVILIAN)) {
      auto victimCollective = [&]() -> optional<string> {
        for (auto col : killer->getPosition().getModel()->getCollectives())
          if (col->getCreatures().contains(victim))
            if (auto& name = col->getName())
              if (name->location)
                return name->location;
        return none;
      }();
      if (victimCollective) {
        auto butcher = killer;
        auto team = teams->getContaining(killer);
        if (!team.empty())
          butcher = teams->getLeader(team[0]);
        if (butcher->addButcheringEvent(*victimCollective))
          addRecordedEvent(TSentence("THE_MASSACRE_OF", TString(*victimCollective)));
      }
    }
  }
}

double Collective::getEfficiency(const Creature* c) const {
  double fromBuffs = 1.0;
  auto f = getGame()->getContentFactory();
  for (auto buff : f->buffsModifyingEfficiency)
    if (c->isAffected(buff))
      fromBuffs *= *f->buffs.at(buff).efficiencyMultiplier;
  return fromBuffs *
      (c->isAffected(LastingEffect::SPEED) ? 1.4 : 1.0) *
      (c->isAffected(LastingEffect::SLOWED) ? (1 / 1.4) : 1.0);
}

const Territory& Collective::getTerritory() const {
  return *territory;
}

Territory& Collective::getTerritory() {
  return *territory;
}

bool Collective::canClaimSquare(Position pos) const {
  return getKnownTiles().isKnown(pos) &&
      pos.isCovered() &&
//      pos.canEnterEmpty({MovementTrait::WALK}) && (seems unnecessary?)
      !pos.isWall();
}

void Collective::unclaimSquare(Position pos) {
  if (!territory->contains(pos))
    return;
  territory->remove(pos);
  for (auto layer : {FurnitureLayer::FLOOR, FurnitureLayer::MIDDLE, FurnitureLayer::CEILING})
    if (auto furniture = pos.modFurniture(layer))
      if (constructions->containsFurniture(pos, layer)) {
        constructions->onFurnitureDestroyed(pos, layer, furniture->getType());
        constructions->removeFurniturePlan(pos, layer);
      }
}

void Collective::claimSquare(Position pos, bool includeStairs) {
  //CHECK(canClaimSquare(pos));
  territory->insert(pos);
  addKnownTile(pos);
  for (auto layer : {FurnitureLayer::FLOOR, FurnitureLayer::MIDDLE, FurnitureLayer::CEILING})
    if (auto furniture = pos.modFurniture(layer))
      if (!furniture->forgetAfterBuilding() && (includeStairs || !furniture->isStairs())) {
        if (!constructions->containsFurniture(pos, furniture->getLayer()))
          constructions->addFurniture(pos, ConstructionMap::FurnitureInfo::getBuilt(furniture->getType()), layer);
        furniture->setTribe(getTribeId());
      }
  control->onClaimedSquare(pos);
}

const KnownTiles& Collective::getKnownTiles() const {
  return *knownTiles;
}

LocalTime Collective::getLocalTime() const {
  return getModel()->getLocalTime();
}

GlobalTime Collective::getGlobalTime() const {
  return getGame()->getGlobalTime();
}

int Collective::numResource(ResourceId id) const {
  PROFILE;
  auto& info = getResourceInfo(id);
  int ret = getValueMaybe(credit, id).value_or(0);
  PositionSet visited;
  for (auto storageId : info.storage)
    for (auto& pos : getStoragePositions(storageId))
      if (!visited.count(pos)) {
        ret += pos.getItems(id).size();
        visited.insert(pos);
      }
  return ret;
}

int Collective::numResourcePlusDebt(ResourceId id) const {
  return numResource(id) - getDebt(id);
}

int Collective::getDebt(ResourceId id) const {
  int ret = constructions->getDebt(id);
  for (auto& elem : taskMap->getCompletionCosts())
    if (elem.second.id == id && !taskMap->getTask(elem.first)->isDone())
      ret -= elem.second.value;
  ret += workshops->getDebt(id);
  return ret;
}

bool Collective::hasResource(const CostInfo& cost) const {
  return numResource(cost.id) >= cost.value;
}

const ResourceInfo& Collective::getResourceInfo(ResourceId id) const {
  return getGame()->getContentFactory()->resourceInfo.at(id);
}

void Collective::takeResource(const CostInfo& cost) {
  int num = cost.value;
  if (num == 0)
    return;
  CHECK(num > 0);
  if (credit[cost.id]) {
    if (credit[cost.id] >= num) {
      credit[cost.id] -= num;
      return;
    } else {
      num -= credit[cost.id];
      credit[cost.id] = 0;
    }
  }
  auto& info = getResourceInfo(cost.id);
  for (auto storageId : info.storage)
    for (auto& pos : getStoragePositions(storageId)) {
      vector<Item*> goldHere = pos.getItems(cost.id);
      for (Item* it : goldHere) {
        pos.removeItem(it);
        if (--num == 0)
          return;
      }
    }
  FATAL << "Not enough " << getResourceInfo(cost.id).name.data() << " missing " << num << " of " << cost.value;
}

void Collective::returnResource(const CostInfo& amount) {
  if (amount.value == 0)
    return;
  CHECK(amount.value > 0);
  auto& info = getResourceInfo(amount.id);
  if (info.itemId) {
    auto items = info.itemId->get(amount.value, getGame()->getContentFactory());
    const auto& destination = getStoragePositions(items[0]->getStorageIds());
    if (!destination.empty()) {
      Random.choose(destination.asVector()).dropItems(std::move(items));
      return;
    }
  }
  credit[amount.id] += amount.value;
}

bool Collective::usesEquipment(const Creature* c) const {
  return config->getManageEquipment()
    && c->getBody().isHumanoid() && !hasTrait(c, MinionTrait::NO_EQUIPMENT)
    && !hasTrait(c, MinionTrait::PRISONER);
}

bool Collective::canUseEquipmentGroup(const Creature* c, const TString& group) {
  for (auto& g : concat({getMinionGroupName(c)}, getAutomatonGroupNames(c)))
    if (!lockedEquipmentGroups.count(g) || !lockedEquipmentGroups.at(g).count(group))
      return true;
  return false;
}

vector<Item*> Collective::getAllItems(bool includeMinions) const {
  return getAllItemsImpl(none, includeMinions);
}

vector<Item*> Collective::getAllItems(ItemIndex index, bool includeMinions) const {
  return getAllItemsImpl(index, includeMinions);
}

vector<pair<Position, vector<Item*>>> Collective::getStoredItems(ItemIndex index, vector<StorageId> storage) const {
  vector<pair<Position, vector<Item*>>> ret;
  for (auto& v : getStoragePositions(storage))
    ret.push_back(make_pair(v, v.getItems(index)));
  return ret;
}

vector<Item*> Collective::getAllItemsImpl(optional<ItemIndex> index, bool includeMinions) const {
  PROFILE;
  vector<Item*> allItems;
  for (auto& v : territory->getAll())
    append(allItems, index ? v.getItems(*index) : v.getItems());
  for (auto& v : zones->getPositions(ZoneId::STORAGE_EQUIPMENT))
    if (!territory->contains(v))
      append(allItems, v.getItems());
  if (includeMinions)
    for (Creature* c : getCreatures())
      append(allItems, index ? c->getEquipment().getItems(*index) : c->getEquipment().getItems());
  return allItems;
}

int Collective::getNumItems(ItemIndex index, bool includeMinions) const {
  int ret = 0;
  for (Position v : territory->getAll())
    ret += v.getItems(index).size();
  if (includeMinions)
    for (Creature* c : getCreatures())
      ret += c->getEquipment().getItems(index).size();
  return ret;
}

void Collective::addKnownVillain(const Collective* col) {
  knownVillains.insert(col);
}

bool Collective::isKnownVillain(const Collective* col) const {
  return (getModel() != col->getModel() && col->getVillainType() != VillainType::NONE) || knownVillains.contains(col);
}

void Collective::addKnownVillainLocation(const Collective* col) {
  knownVillainLocations.insert(col);
  if (auto stringId = immigration->suppliesRecruits(col))
    control->addWindowMessage(ViewIdList{col->getName()->viewId}, TSentence(*stringId, col->getName()->full));
  if (col->hasTradeItems() && !getTribe()->isEnemy(col->getTribe()))
    control->addWindowMessage(ViewIdList{col->getName()->viewId}, TSentence("TRADING_AVAILABLE", col->getName()->full));
}

bool Collective::isKnownVillainLocation(const Collective* col) const {
  return knownVillainLocations.contains(col);
}

const Task* Collective::getItemTask(const Item* it) const {
  return markedItems.getOrElse(it, nullptr).get();
}

void Collective::markItem(const Item* it, const Task* task) {
  CHECK(!getItemTask(it));
  markedItems.set(it, task);
}

bool Collective::canAddFurniture(Position position, FurnitureType type) const {
  auto& furniture = getGame()->getContentFactory()->furniture.getData(type);
  if (auto& pred = furniture.getUsagePredicate())
    if (!pred->apply(position, nullptr))
      return false;
  auto layer = furniture.getLayer();
  return knownTiles->isKnown(position)
      && ((position.isCovered() && (territory->contains(position) || canClaimSquare(position))) ||
          furniture.buildOutsideOfTerritory())
      && !getConstructions().containsFurniture(position, layer)
      && position.canConstruct(type);
}

void Collective::removeUnbuiltFurniture(Position pos, FurnitureLayer layer) {
  if (auto f = constructions->getFurniture(pos, layer)) {
    if (f->hasTask())
      returnResource(taskMap->removeTask(f->getTask()));
    constructions->removeFurniturePlan(pos, layer);
  }
}

static ZoneId destroyedOnOrder[] = {
  ZoneId::FETCH_ITEMS,
  ZoneId::PERMANENT_FETCH_ITEMS,
  ZoneId::STORAGE_EQUIPMENT,
  ZoneId::STORAGE_RESOURCES,
  ZoneId::GUARD1,
  ZoneId::GUARD2,
  ZoneId::GUARD3
};

void Collective::destroyOrder(Position pos, FurnitureLayer layer) {
  auto furniture = pos.modFurniture(layer);
  if (!furniture || furniture->canRemoveWithCreaturePresent() || !pos.getCreature()) {
    if (furniture && !furniture->canDestroy(DestroyAction::Type::DIG) && !furniture->forgetAfterBuilding() &&
        (furniture->getTribe() == getTribeId() || furniture->canRemoveNonFriendly())) {
      furniture->destroy(pos, DestroyAction::Type::BASH);
    }
    if (!furniture || (!furniture->canDestroy(DestroyAction::Type::DIG) && !furniture->forgetAfterBuilding())) {
      removeUnbuiltFurniture(pos, layer);
      if (furniture)
        if (auto otherPos = furniture->getSecondPart(pos))
          removeUnbuiltFurniture(*otherPos, layer);
    }
  }
  if (layer == FurnitureLayer::MIDDLE)
    for (auto id : destroyedOnOrder)
      if (zones->isZone(pos, id))
        eraseZone(pos, id);
}

void Collective::addFurniture(Position pos, FurnitureType type, const CostInfo& cost, bool noCredit) {
  if (!noCredit || hasResource(cost)) {
    auto layer = getGame()->getContentFactory()->furniture.getData(type).getLayer();
    constructions->addFurniture(pos, ConstructionMap::FurnitureInfo(type, cost), layer);
    updateConstructions();
  }
}

const ConstructionMap& Collective::getConstructions() const {
  return *constructions;
}

ConstructionMap& Collective::getConstructions() {
  return *constructions;
}

void Collective::cancelMarkedTask(Position pos) {
  taskMap->removeTask(taskMap->getMarked(pos));
}

void Collective::setPriorityTasks(Position pos) {
  for (Task* t : taskMap->getTasks(pos))
    taskMap->setPriorityTask(t);
  pos.setNeedsRenderUpdate(true);
}

bool Collective::hasPriorityTasks(Position pos) const {
  return taskMap->hasPriorityTasks(pos);
}

void Collective::setSteed(Creature* rider, Creature* steed) {
  if (!!steed && steed == rider->getSteedCompanion())
    steed = rider;
  auto setImpl = [this] (Creature* c1, Creature* c2) {
    if (auto current = getSteedOrRider(c1))
      steedAssignments.erase(current);
    if (c2)
      steedAssignments.set(c1, c2);
    else
      steedAssignments.erase(c1);
  };
  if (rider)
    setImpl(rider, steed);
  if (steed && steed != rider)
    setImpl(steed, rider);
}

Creature* Collective::getSteedOrRider(Creature* minion) {
  auto ret = steedAssignments.getMaybe(minion).value_or(nullptr);
  if (ret == minion)
    return minion->getSteedCompanion();
  return ret;
}

static HighlightType getHighlight(const DestroyAction& action) {
  switch (action.getType()) {
    case DestroyAction::Type::CUT:
      return HighlightType::CUT_TREE;
    default:
      return HighlightType::DIG;
  }
}

void Collective::orderDestruction(Position pos, const DestroyAction& action) {
  const auto layer = FurnitureLayer::MIDDLE;
  auto f = NOTNULL(pos.getFurniture(layer));
  CHECK(f->canDestroy(action));
  taskMap->markSquare(pos, getHighlight(action), Task::destruction(this, pos, f, action,
      pos.canEnterEmpty({MovementTrait::WALK}) ? nullptr : positionMatching.get()),
      action.getMinionActivity());
}

bool Collective::isConstructionReachable(Position pos) {
  PROFILE;
  return knownTiles->getKnownTilesWithMargin().count(pos);
}

bool Collective::containsCreature(Creature* c) {
  return creatures.contains(c);
}

void Collective::recalculateFurniturePopIncrease() {
  populationIncrease = 0;
  auto& factory = getGame()->getContentFactory()->furniture;
  for (auto type : factory.getFurnitureThatIncreasePopulation()) {
    auto& info = factory.getData(type);
    if (info.getPopulationIncrease().requiresAnimalFence) {
      int count = 0;
      for (auto pos : constructions->getBuiltPositions(type))
        if (pos.isClosedOff(MovementType(MovementTrait::WALK).setFarmAnimal()))
          ++count;
      populationIncrease += factory.getPopulationIncrease(type, count);
    } else
      populationIncrease += factory.getPopulationIncrease(type, constructions->getBuiltCount(type));
  }
}

void Collective::onConstructed(Position pos, FurnitureType type) {
  if (pos.getFurniture(type)->forgetAfterBuilding()) {
    constructions->removeFurniturePlan(pos, getGame()->getContentFactory()->furniture.getData(type).getLayer());
    if (territory->contains(pos))
      territory->remove(pos);
    control->onConstructed(pos, type);
    return;
  }
  constructions->onConstructed(pos, type);
  control->onConstructed(pos, type);
  if (Task* task = taskMap->getMarked(pos))
    taskMap->removeTask(task);
  claimSquare(pos);
  for (auto v : pos.neighbors8()) // check for furniture as a workaround to not claim the zombie zlevel gate when building a bridge
    if (v.isCovered() && !v.isUnavailable() && !v.getFurniture(FurnitureLayer::MIDDLE))
      claimSquare(v);
  recalculateFurniturePopIncrease();
}


void Collective::onDestructed(Position pos, FurnitureType type, const DestroyAction& action) {
  removeUnbuiltFurniture(pos, getGame()->getContentFactory()->furniture.getData(type).getLayer());
  switch (action.getType()) {
    case DestroyAction::Type::CUT:
      zones->setZone(pos, ZoneId::FETCH_ITEMS);
      break;
    case DestroyAction::Type::DIG:
      territory->insert(pos);
      break;
    default:
      break;
  }
  control->onDestructed(pos, type, action);
}

void Collective::updateConstructions() {
  PROFILE;
  for (auto& pos : constructions->getAllFurniture()) {
    auto& construction = *constructions->getFurniture(pos.first, pos.second);
    if (!isDelayed(pos.first) &&
        !construction.hasTask() &&
        pos.first.canConstruct(construction.getFurnitureType()) &&
        !construction.isBuilt(pos.first, pos.second) &&
        hasResource(construction.getCost())) {
      constructions->setTask(pos.first, pos.second,
          taskMap->addTaskCost(Task::construction(this, pos.first, construction.getFurnitureType()), pos.first,
              construction.getCost(), MinionActivity::CONSTRUCTION)->getUniqueId());
      takeResource(construction.getCost());
    }
  }
}

void Collective::delayDangerousTasks(const vector<Position>& enemyPos1, LocalTime delayTime) {
  PROFILE;
  HashSet<Level*> levels;
  for (auto& pos : enemyPos1)
    levels.insert(pos.getLevel());
  for (auto& level : levels) {
    vector<Vec2> enemyPos = enemyPos1
        .filter([=] (const Position& p) { return p.isSameLevel(level); })
        .transform([] (const Position& p) { return p.getCoord();});
    int infinity = 1000000;
    int radius = 10;
    Table<int> dist(Rectangle::boundingBox(enemyPos)
        .minusMargin(-radius)
        .intersection(level->getBounds()), infinity);
    queue<Vec2> q;
    for (Vec2 v : enemyPos) {
      dist[v] = 0;
      q.push(v);
    }
    while (!q.empty()) {
      Vec2 pos = q.front();
      q.pop();
      delayedPos[Position(pos, level)] = delayTime;
      if (dist[pos] >= radius)
        continue;
      for (Vec2 v : pos.neighbors8())
        if (v.inRectangle(dist.getBounds()) && dist[v] == infinity && territory->contains(Position(v, level))) {
          dist[v] = dist[pos] + 1;
          q.push(v);
        }
    }
  }
}

bool Collective::isDelayed(Position pos) {
  PROFILE
  return delayedPos.count(pos) && delayedPos.at(pos) > getLocalTime();
}

static Position chooseClosest(Position pos, const StoragePositions& squares) {
  optional<Position> ret;
  for (auto& p : squares)
    if (!ret || pos.dist8(p).value_or(10000) < pos.dist8(*ret).value_or(10000))
      ret = p;
  return *ret;
}

const StoragePositions& Collective::getStoragePositions(StorageId storage) const {
  return constructions->getStoragePositions(storage);
}

const StoragePositions& Collective::getStoragePositions(const vector<StorageId>& storage) const {
  for (auto id : storage) {
    auto& ret = getStoragePositions(id);
    if (!ret.empty())
      return ret;
  }
  static StoragePositions empty;
  return empty;
}

void Collective::fetchItems(Position pos) {
  PROFILE;
  if (!Random.roll(30) && constructions->getAllStoragePositions().contains(pos))
    return;
  auto items = pos.getItems();
  if (!items.empty()) {
    HashMap<StorageId, vector<Item*>> itemMap;
    for (auto it : items)
      if (!getItemTask(it))
        for (auto id : it->getStorageIds()) {
          auto& positions = getStoragePositions(id);
          if (!positions.empty()) {
            if (!positions.contains(pos))
              itemMap[id].push_back(it);
            break;
          }
        }
    for (auto& elem : itemMap) {
      //warnings->setWarning(elem.warning, false);
      auto pickUpAndDrop = Task::pickUpAndDrop(pos, elem.second, {elem.first}, this);
      auto task = taskMap->addTask(std::move(pickUpAndDrop.pickUp), pos, MinionActivity::HAULING);
      taskMap->addTask(std::move(pickUpAndDrop.drop), chooseClosest(pos, getStoragePositions(elem.first)),
          MinionActivity::HAULING);
      for (Item* it : elem.second)
        markItem(it, task);
    }/* else
      warnings->setWarning(elem.warning, true);*/
  }
}

void Collective::handleSurprise(Position pos) {
  Vec2 rad(8, 8);
  for (Position v : Random.permutation(pos.getRectangle(Rectangle(-rad, rad + Vec2(1, 1)))))
    if (Creature* other = v.getCreature())
      if (hasTrait(other, MinionTrait::FIGHTER) && *v.dist8(pos) > 1) {
        for (Position dest : pos.neighbors8(Random))
          if (v.canMoveCreature(dest)) {
            v.moveCreature(dest, true);
            break;
          }
      }
  pos.globalMessage(TStringId("SURPRISE_EXCLAMATION"));
}

void Collective::retire() {
  knownTiles->limitToModel(getModel());
  knownVillainLocations.clear();
  knownVillains.clear();
  discoverable = true;
  config->setConquerCondition(ConquerCondition::KILL_FIGHTERS_AND_LEADER);
  // Clear non-minions from byTrait to workaround some bug where it contains bogus creatures.
  for (auto trait : ENUM_ALL(MinionTrait))
    byTrait[trait] = byTrait[trait].filter([this](Creature* c) { return creatures.contains(c); });
}

CollectiveWarnings& Collective::getWarnings() {
  return *warnings;
}

CollectiveConfig& Collective::getConfig() {
  return *config;
}

bool Collective::addKnownTile(Position pos) {
  PROFILE;
  if (!knownTiles->isKnown(pos)) {
    pos.setNeedsRenderAndMemoryUpdate(true);
    knownTiles->addTile(pos, getModel());
    if (Task* task = taskMap->getMarked(pos))
      if (task->isBogus())
        taskMap->removeTask(task);
    return true;
  } else
    return false;
}

void Collective::summonDemon(Creature* c) {
  auto id = Random.choose(CreatureId("SPECIAL_BLGN"), CreatureId("SPECIAL_BLGW"), CreatureId("SPECIAL_HLGN"), CreatureId("SPECIAL_HLGW"));
  Effect::summon(c, id, 1, 500_visible);
  auto message = PlayerMessage(TSentence("HAS_SUMMONED_DEMON", c->getName().the()), MessagePriority::CRITICAL);
  c->thirdPerson(message);
  control->addMessage(message);
  getGame()->addAnalytics("milestone", "poetryDemon");
}

bool Collective::usesEfficiency(const Furniture* f) const {
  if (isOneOf(f->getType(),
          FurnitureType("POETRY_TABLE"),
          FurnitureType("PAINTING_N"),
          FurnitureType("PAINTING_S"),
          FurnitureType("PAINTING_E"),
          FurnitureType("PAINTING_W"),
          FurnitureType("FURNACE")))
    return true;
  if (auto usage = f->getUsageType())
    if (auto id = usage->getReferenceMaybe<BuiltinUsageId>())
      switch (*id) {
        case BuiltinUsageId::DEMON_RITUAL:
        case BuiltinUsageId::TRAIN:
        case BuiltinUsageId::STUDY:
        case BuiltinUsageId::ARCHERY_RANGE:
          return true;
        default:
          break;
      }
  if (auto workshopType = getGame()->getContentFactory()->getWorkshopType(f->getType()))
    if (getReferenceMaybe(workshops->types, *workshopType))
      return true;
  return false;
}

void Collective::onAppliedSquare(Creature* c, pair<Position, FurnitureLayer> pos) {
  PROFILE;
  auto contentFactory = getGame()->getContentFactory();
  if (auto furniture = pos.first.getFurniture(pos.second)) {
    if (auto& pred = furniture->getUsagePredicate())
      if (!pred->apply(pos.first, c))
        return;
    // Furniture have variable usage time, so just multiply by it to be independent of changes.
    double efficiency = furniture->getUsageTime().getVisibleDouble() * getEfficiency(c)
        * pos.first.getLuxuryEfficiencyMultiplier();
    if (furniture->isRequiresLight())
      efficiency *= pos.first.getLightingEfficiency();
    if (furniture->getType() == FurnitureType("WHIPPING_POST"))
      taskMap->addTask(Task::whipping(pos.first, c), pos.first, MinionActivity::WORKING);
    if (furniture->getType() == FurnitureType("GALLOWS"))
      taskMap->addTask(Task::kill(c), pos.first, MinionActivity::WORKING);
    if (furniture->getType() == FurnitureType("TORTURE_TABLE"))
      taskMap->addTask(Task::torture(c), pos.first, MinionActivity::WORKING);
    if (furniture->getType() == FurnitureType("WITCH_LABORATORY") && c->getStatus().contains(CreatureStatus::PRISONER)) {
      control->addMessage(TStringId("A_CHILD_IS_COOKED"));
      c->dieNoReason(Creature::DropType::ONLY_INVENTORY);
      returnResource(CostInfo(CollectiveResourceId("COOKED_CHILDREN"), 1));
    }
    if (furniture->getType() == FurnitureType("POETRY_TABLE") && Random.chance(0.01 * efficiency)) {
      auto poem = ItemType(ItemTypes::Poem{}).get(contentFactory);
      bool demon = Random.roll(500);
      if (!recordedEvents.empty() && Random.roll(3)) {
        auto event = Random.choose(recordedEvents);
        recordedEvents.erase(event);
        poem = ItemType(ItemTypes::EventPoem{event}).get(contentFactory);
        demon = false;
      }
      control->addMessage(TSentence("WRITES_A", c->getName().a(), poem->getAName()));
      c->getPosition().dropItem(std::move(poem));
      if (demon)
        summonDemon(c);
    }
    if (Random.chance(0.01 * efficiency) &&
        (furniture->getType() == FurnitureType("PAINTING_N") ||
         furniture->getType() == FurnitureType("PAINTING_S") ||
         furniture->getType() == FurnitureType("PAINTING_E") ||
         furniture->getType() == FurnitureType("PAINTING_W"))) {
      bool demon = Random.roll(500);
      if (!recordedEvents.empty()|| demon) {
        TString name = TSentence("PAINTING_DEPICTING", [&]() -> TString {
          if (demon) {
            summonDemon(c);
            return TStringId("A_DEMON");
          } else {
            auto event = Random.choose(recordedEvents);
            recordedEvents.erase(event);
            return event;
          }
        }());
        auto viewId = string(furniture->getViewObject()->id().data());
        auto f = pos.first.modFurniture(furniture->getLayer());
        f->getViewObject()->setId(ViewId(("painting" + viewId.substr(viewId.size() - 2)).data()));
        f->setName(name);
        if (!demon) {
          auto sentence = TSentence("MAKES_A", c->getName().a(), name);
          c->thirdPerson(sentence);
          control->addMessage(sentence);
        }
        return;
      }
    }
    if (auto usage = furniture->getUsageType()) {
      auto increaseLevel = [&] (AttrType exp) {
        double increase = 0.012 * efficiency * LastingEffects::getTrainingSpeed(c);
        if (auto maxLevel = contentFactory->furniture.getData(furniture->getType()).getMaxTraining(exp))
          increase = min(increase, maxLevel - c->getAttributes().getExpLevel(exp));
        if (increase > 0)
          c->increaseExpLevel(exp, increase);
      };
      if (auto id = usage->getReferenceMaybe<BuiltinUsageId>())
        switch (*id) {
          case BuiltinUsageId::DEMON_RITUAL: {
            vector<Creature*> toHeal;
            for (auto c : getCreatures())
              if (c->getBody().canHeal(HealthType::SPIRIT, contentFactory))
                toHeal.push_back(c);
            if (!toHeal.empty()) {
              for (auto c : toHeal)
                c->heal(double(efficiency) * 0.05 / toHeal.size());
            } else
              returnResource(CostInfo(ResourceId("DEMON_PIETY"), int(efficiency)));
            break;
          }
          case BuiltinUsageId::TRAIN:
            increaseLevel(AttrType("DAMAGE"));
            break;
          case BuiltinUsageId::STUDY:
            increaseLevel(AttrType("SPELL_DAMAGE"));
            break;
          case BuiltinUsageId::ARCHERY_RANGE:
            increaseLevel(AttrType("RANGED_DAMAGE"));
            break;
          case BuiltinUsageId::PRAY:
            increaseLevel(AttrType("DIVINITY"));
            break;
          default:
            break;
        }
    }
    if (furniture->getType() == FurnitureType("FURNACE")) {
      auto craftingSkill = double(c->getAttr(AttrType("FURNACE"))) * 0.02;
      if (auto result = furnace->addWork(efficiency * craftingSkill * LastingEffects::getCraftingSpeed(c)))
        returnResource(*result);
    }
    if (auto workshopType = contentFactory->getWorkshopType(furniture->getType()))
      if (auto workshop = getReferenceMaybe(workshops->types, *workshopType)) {
        auto& workshopInfo = contentFactory->workshopInfo.at(*workshopType);
        auto craftingSkill = c->getAttr(workshopInfo.attr);
        double speedBoost = 1.0;
        for (auto next : pos.first.neighbors8())
          if (auto f = next.getFurniture(FurnitureLayer::MIDDLE))
            for (auto& boost : f->workshopSpeedBoost)
              if (boost.type == *workshopType)
                speedBoost = max(speedBoost, boost.multiplier);
        auto item = workshop->addWork(this, efficiency * double(craftingSkill) * 0.02
            * LastingEffects::getCraftingSpeed(c) * speedBoost, craftingSkill, workshopInfo.prefix);
        if (item) {
          if (item->getClass() == ItemClass::WEAPON)
            getGame()->getStatistics().add(StatId::WEAPON_PRODUCED);
          if (item->getClass() == ItemClass::ARMOR)
            getGame()->getStatistics().add(StatId::ARMOR_PRODUCED);
          if (auto stat = item->getProducedStat())
            getGame()->getStatistics().add(*stat);
          control->addMessage(TSentence("MINION_PRODUCES", {c->getName().a(), workshopInfo.verb, item->getAName()}));
          if (item->getName() == TStringId("ITEMS_DEVOTIONALMEDAL_NAME") && !recordedEvents.empty())
            item->setDescription(TSentence("DEPICTS", Random.choose(recordedEvents)));
          if (auto& minion = item->getAssembledMinion())
            minion->assemble(this, item.get(), pos.first);
          else
            c->getEquipment().addItem(std::move(item), c, contentFactory);
        }
    }
  }
}

optional<FurnitureType> Collective::getMissingTrainingFurniture(const Creature* c, AttrType attr) const {
  if (c->getAttributes().isTrainingMaxedOut(attr))
    return none;
  optional<FurnitureType> requiredDummy;
  for (auto dummyType : getGame()->getContentFactory()->furniture.getTrainingFurniture(attr)) {
    bool canTrain = getGame()->getContentFactory()->furniture.getData(dummyType).getMaxTraining(attr) >
        c->getAttributes().getExpLevel(attr);
    bool hasDummy = getConstructions().getBuiltCount(dummyType) > 0;
    if (canTrain && hasDummy)
      return none;
    if (!requiredDummy && canTrain && !hasDummy)
      requiredDummy = dummyType;
  }
  return requiredDummy;
}

double Collective::getDangerLevel() const {
  if (!dangerLevelCache) {
    double ret = 0;
    for (const Creature* c : getCreatures(MinionTrait::FIGHTER))
      ret += c->getDifficultyPoints();
    ret += constructions->getBuiltCount(FurnitureType("IMPALED_HEAD")) * 150;
    dangerLevelCache = ret;
  }
  return *dangerLevelCache;
}

void Collective::acquireTech(TechId tech, bool throughLevelling) {
  technology->researched.insert(tech);
  if (throughLevelling)
    ++dungeonLevel.consumedLevels;
}

const Technology& Collective::getTechnology() const {
  return *technology;
}

void Collective::setTechnology(Technology t) {
  technology = std::move(t);
}

const EntitySet<Creature>& Collective::getKills() const {
  return kills;
}

int Collective::getPoints() const {
  return points;
}

void Collective::onRansomPaid() {
  control->onRansomPaid();
}

void Collective::onExternalEnemyKilled(const TString& name) {
  control->addMessage(PlayerMessage(TSentence("YOU_RESISTED_THE_ATTACK_OF", name),
      MessagePriority::CRITICAL));
  dungeonLevel.onKilledWave();
}

const HashSet<TString>& Collective::getRecordedEvents() const {
  return recordedEvents;
}

void Collective::addRecordedEvent(TString s) {
  if (!allRecordedEvents.count(s)) {
    allRecordedEvents.insert(s);
    recordedEvents.insert(std::move(s));
  }
}

void Collective::onCopulated(Creature* who, Creature* with) {
  PROFILE;
  control->addMessage(TSentence("COPULATES", who->getName().a(), with->getName().a()));
  who->getAttributes().copulationClientEffect.apply(with->getPosition(), who);
  who->getAttributes().copulationEffect.apply(who->getPosition());
  addRecordedEvent(TSentence("A_SEX_ACT_BETWEEN", who->getName().a(), with->getName().a()));
  setMinionActivity(who, MinionActivity::IDLE);
}

MinionEquipment& Collective::getMinionEquipment() {
  return *minionEquipment;
}

const MinionEquipment& Collective::getMinionEquipment() const {
  return *minionEquipment;
}

void Collective::autoAssignEquipment(Creature* creature) {
  auto items = getAllItems(ItemIndex::MINION_EQUIPMENT, false).filter([&](auto item) {
    auto g = item->getEquipmentGroup();
    return item->getAutoEquipPredicate().apply(creature, nullptr) && (!g || canUseEquipmentGroup(creature, *g));
  });
  minionEquipment->autoAssign(creature, items);
}


Workshops& Collective::getWorkshops() {
  return *workshops;
}

const Workshops& Collective::getWorkshops() const {
  return *workshops;
}

Furnace& Collective::getFurnace() {
  return *furnace;
}

const Furnace& Collective::getFurnace() const {
  return *furnace;
}

Dancing& Collective::getDancing() {
  return *dancing;
}

Immigration& Collective::getImmigration() {
  return *immigration;
}

const Immigration& Collective::getImmigration() const {
  return *immigration;
}

const MinionActivities& Collective::getMinionActivities() const {
  return *minionActivities;
}

CollectiveTeams& Collective::getTeams() {
  return *teams;
}

const CollectiveTeams& Collective::getTeams() const {
  return *teams;
}

void Collective::freeTeamMembers(const vector<Creature*>& members) {
  PROFILE;
  for (Creature* c : members) {
    if (c->isAffected(LastingEffect::SLEEP))
      c->removeEffect(LastingEffect::SLEEP);
  }
}

const Zones& Collective::getZones() const {
  return *zones;
}

static optional<StorageId> getZoneStorage(ZoneId id) {
  switch (id) {
    case ZoneId::STORAGE_EQUIPMENT:
      return StorageId{"equipment"};
    case ZoneId::STORAGE_RESOURCES:
      return StorageId{"resources"};
    default:
      return none;
  }
}

void Collective::setZone(Position pos, ZoneId id) {
  zones->setZone(pos, id);
  if (auto storageId = getZoneStorage(id)) {
    constructions->getStoragePositions(*storageId).add(pos);
    constructions->getAllStoragePositions().add(pos);
  }
}

void Collective::eraseZone(Position pos, ZoneId id) {
  if (id == ZoneId::QUARTERS) {
    auto quarters = zones->getQuartersInfo(pos);
    if (quarters->id) {
      auto matches = creatures.filter([&](Creature* c){
        return c && c->getUniqueId() == quarters->id;
      });

      if (!matches.empty()) {
        Creature* found = matches.front();
        found->getAttributes().setHasQuarters(false);
      }
    }
  }
  zones->eraseZone(pos, id);
  if (auto storageId = getZoneStorage(id)) {
    constructions->getStoragePositions(*storageId).remove(pos);
    constructions->getAllStoragePositions().remove(pos);
  }
}

void Collective::assignQuarters(Creature* c, Position pos) {
  auto quarters = zones->getQuartersInfo(pos);    
  zones->assignQuarters(c->getUniqueId(), pos);
  c->getAttributes().setHasQuarters(true);

  if (quarters->id) {
      auto matches = creatures.filter([&](Creature* c){
        return c && c->getUniqueId() == quarters->id;
      });

      if (!matches.empty()) {
        Creature* found = matches.front();
        if (found->getUniqueId() != c->getUniqueId())
          found->getAttributes().setHasQuarters(false);
      }
  }
}

const TaskMap& Collective::getTaskMap() const {
  return *taskMap;
}

TaskMap& Collective::getTaskMap() {
  return *taskMap;
}

int Collective::getPopulationSize() const {
  return populationGroups.size() - getCreatures(MinionTrait::NO_LIMIT).size();
}

int Collective::getMaxPopulation() const {
  return populationIncrease + config->getMaxPopulation() + getCreatures(MinionTrait::INCREASE_POPULATION).size();
}

const DungeonLevel& Collective::getDungeonLevel() const {
  return dungeonLevel;
}

DungeonLevel& Collective::getDungeonLevel() {
  return dungeonLevel;
}

REGISTER_TYPE(Collective)
REGISTER_TYPE(ListenerTemplate<Collective>)
