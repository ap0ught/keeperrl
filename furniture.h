#pragma once

#include "destroy_action.h"
#include "fire.h"
#include "item_factory.h"
#include "vision_id.h"
#include "event_listener.h"
#include "furniture_layer.h"
#include "furniture_type.h"
#include "attr_type.h"
#include "bed_type.h"
#include "fx_info.h"
#include "view_id.h"
#include "effect.h"
#include "creature_id.h"
#include "fx_variant_name.h"
#include "furniture_usage.h"
#include "furniture_tick.h"
#include "storage_id.h"
#include "lasting_or_buff.h"
#include "creature_predicate.h"
#include "achievement_id.h"
#include "sound.h"
#include "t_string.h"
#include "workshop_type.h"

class TribeId;
class Creature;
class MovementType;
class Fire;
class ItemFactory;
class GameEvent;
class Position;
class FurnitureEntry;
class FurnitureDroppedItems;
class ViewObject;
class MovementSet;
class Effect;
class FurnitureUsageType;

RICH_ENUM(
    BurnsDownMessage,
    BURNS_DOWN,
    STOPS_BURNING
);

RICH_ENUM(
    ConstructMessage,
    BUILD, /*default*/
    FILL_UP,
    REINFORCE,
    SET_UP
);

struct FurnitureEffectInfo {
  enum class Target;
  Target SERIAL(target);
  LastingOrBuff SERIAL(effect);
  int SERIAL(radius);
  SERIALIZE_ALL(target, effect, radius)
};

RICH_ENUM(FurnitureEffectInfo::Target,
    ALLY,
    ENEMY
);

class Furniture {
  public:

  Furniture(const Furniture&);
  Furniture(Furniture&&) noexcept;
  Furniture& operator =(Furniture&&);
  Furniture& operator =(const Furniture&);
  const heap_optional<ViewObject>& getViewObject() const;
  heap_optional<ViewObject>& getViewObject();
  const TString& getName(int count = 1) const;
  void setName(const TString&);
  FurnitureType getType() const;
  bool isVisibleTo(const Creature*) const;
  const MovementSet& getMovementSet() const;
  void onEnter(Creature*) const;
  void onItemsRemoved(Position) const;
  bool canDestroy(const Position&, const MovementType&, const DestroyAction&) const;
  bool canDestroy(const DestroyAction&) const;
  optional<double> getStrength(const DestroyAction&) const;
  void destroy(Position, const DestroyAction&, Creature* destroyedBy = nullptr);
  void tryToDestroyBy(Position, Creature*, const DestroyAction&);
  TribeId getTribe() const;
  void setTribe(TribeId);
  const heap_optional<Fire>& getFire() const;
  bool fireDamage(Position, bool withMessage = true);
  bool acidDamage(Position);
  bool iceDamage(Position);
  void tick(Position, FurnitureLayer supposedLayer);
  void updateFire(Position, FurnitureLayer supposedLayer);
  bool canSeeThru(VisionId) const;
  bool blocksAnyVision() const;
  bool stopsProjectiles(VisionId) const;
  void click(Position) const;
  bool overridesMovement() const;
  void use(Position, Creature*) const;
  optional<Position> getSecondPart(Position) const;
  bool isStairs() const;
  bool canUse(const Creature*) const;
  optional<FurnitureUsageType> getUsageType() const;
  bool hasUsageType(BuiltinUsageId) const;
  TimeInterval getUsageTime() const;
  optional<FurnitureClickType> getClickType() const;
  optional<FurnitureTickType> getTickType() const;
  const heap_optional<FurnitureEntry>& getEntryType() const;
  heap_optional<FurnitureEntry>& getEntryType();
  bool isTicking() const;
  bool isWall() const;
  bool isBuildingSupport() const;
  void onConstructedBy(Position, Creature*);
  void beforeRemoved(Position) const;
  FurnitureLayer getLayer() const;
  double getLightEmission() const;
  bool canHide() const;
  bool emitsWarning(const Creature*) const;
  bool canRemoveWithCreaturePresent() const;
  bool canRemoveNonFriendly() const;
  Creature* getCreator() const;
  optional<LocalTime> getCreatedTime() const;
  optional<CreatureId> getSummonedElement() const;
  bool isClearFogOfWar() const;
  bool forgetAfterBuilding() const;
  void onCreatureWalkedOver(Position, Vec2 direction) const;
  void onCreatureWalkedInto(Position, Vec2 direction) const;
  bool onBloodNear(Position);
  bool hasBlood() const;
  void spreadBlood(Position);
  int getMaxTraining(AttrType) const;
  const HashMap<AttrType, int>& getMaxTraining() const;
  bool hasRequiredSupport(Position) const;
  optional<FurnitureType> getDiningFurnitureType() const;
  const optional<CreaturePredicate>& getUsagePredicate() const;
  optional<ViewId> getSupportViewId(Position) const;
  optional<FurnitureType> getUpgrade() const;
  optional<FXVariantName> getUsageFX() const;
  /**
   * @brief Calls special functionality to handle dropped items, if any.
   * @return possibly empty subset of the items that weren't consumned and can be dropped normally.
   */
  vector<PItem> dropItems(Position, vector<PItem>) const;
  optional<FurnitureType> getDefaultBridge() const;
  optional<FurnitureType> getFillPit() const;
  double getLuxury() const;
  struct PopulationInfo {
    double SERIAL(increase);
    optional<int> SERIAL(limit);
    bool SERIAL(requiresAnimalFence) = false;
    SERIALIZE_ALL(NAMED(increase), NAMED(limit), OPTION(requiresAnimalFence))
  };
  const PopulationInfo& getPopulationIncrease() const;
  const vector<FurnitureType>& getBuiltOver() const;
  bool isBridge() const;
  bool silentlyReplace() const;
  void setType(FurnitureType);
  bool buildOutsideOfTerritory() const;
  bool isRequiresLight() const;
  bool isHostileSpell() const;
  bool isEyeball() const;
  optional<BedType> getBedType() const;
  const optional<FurnitureEffectInfo>& getLastingEffectInfo() const;
  const heap_optional<ItemList>& getItemDrop() const;
  const vector<StorageId>& getStorageId() const;
  bool doesHideItems() const;
  const optional<ViewId>& getEmptyViewId() const;
  const optional<AchievementId>& getMinedAchievement() const;
  bool canRemoveInstantly() const;
  bool isBuildingFloor() const;
  optional<FurnitureType> getOtherStairs() const;

  Furniture& setBlocking();
  Furniture& setBlockingEnemies();
  Furniture& setDestroyable(double);
  Furniture& setDestroyable(double, DestroyAction::Type);

  SERIALIZATION_DECL(Furniture)

  ~Furniture();

  optional<FurnitureTickType> SERIAL(tickType);
  struct WorkshopBoost {
    double SERIAL(multiplier);
    WorkshopType (type);
    SERIALIZE_ALL(multiplier, type);
  };

  vector<WorkshopBoost> SERIAL(workshopSpeedBoost);

  private:
  heap_optional<ViewObject> SERIAL(viewObject);
  TString SERIAL(name);
  TString SERIAL(pluralName);
  FurnitureType SERIAL(type);
  FurnitureLayer SERIAL(layer) = FurnitureLayer::MIDDLE;
  HeapAllocated<MovementSet> SERIAL(movementSet);
  heap_optional<Fire> SERIAL(fire);
  optional<FurnitureType> SERIAL(burntRemains);
  optional<FurnitureType> SERIAL(destroyedRemains);
  struct DestroyedInfo {
    double SERIAL(health);
    double SERIAL(strength);
    SERIALIZE_ALL(health, strength)
  };
  EnumMap<DestroyAction::Type, optional<DestroyedInfo>> SERIAL(destroyedInfo);
  heap_optional<ItemList> SERIAL(itemDrop);
  EnumSet<VisionId> SERIAL(blockVision);
  optional<FurnitureUsageType> SERIAL(usageType);
  optional<FurnitureClickType> SERIAL(clickType);
  optional<FurnitureOnBuilt> SERIAL(onBuilt);
  heap_optional<FurnitureEntry> SERIAL(entryType);
  heap_optional<FurnitureDroppedItems> SERIAL(droppedItems);
  TimeInterval SERIAL(usageTime) = 1_visible;
  bool SERIAL(overrideMovement) = false;
  bool SERIAL(removeWithCreaturePresent) = true;
  bool SERIAL(removeNonFriendly) = false;
  bool SERIAL(wall) = false;
  optional<ConstructMessage> SERIAL(constructMessage) = ConstructMessage::BUILD;
  double SERIAL(lightEmission) = 0;
  bool SERIAL(canHideHere) = false;
  bool SERIAL(warning) = false;
  WeakPointer<Creature> SERIAL(creator);
  optional<LocalTime> SERIAL(createdTime);
  optional<CreatureId> SERIAL(summonedElement);
  optional<FurnitureType> SERIAL(defaultBridge);
  optional<FurnitureType> SERIAL(fillPit);
  bool SERIAL(noProjectiles) = false;
  bool SERIAL(clearFogOfWar) = false;
  bool SERIAL(xForgetAfterBuilding) = false;
  double SERIAL(luxury) = 0;
  void updateViewObject();
  BurnsDownMessage SERIAL(burnsDownMessage) = BurnsDownMessage::BURNS_DOWN;
  template<typename Archive>
  void serializeImpl(Archive&, const unsigned);
  HashMap<AttrType, int> SERIAL(maxTraining);
  struct SupportInfo {
    vector<Dir> SERIAL(dirs);
    optional<ViewId> SERIAL(viewId);
    SERIALIZE_ALL(NAMED(dirs), NAMED(viewId))
  };
  vector<SupportInfo> SERIAL(requiredSupport);
  const Furniture::SupportInfo* getSupportInfo(Position) const;
  optional<FurnitureType> SERIAL(upgrade);
  vector<FurnitureType> SERIAL(builtOver);
  bool SERIAL(bridge) = false;
  bool SERIAL(canSilentlyReplace) = false;
  bool SERIAL(canBuildOutsideOfTerritory) = false;
  bool SERIAL(requiresLight) = false;
  optional<BedType> SERIAL(bedType);
  PopulationInfo SERIAL(populationIncrease) = {0, none};
  optional<FXInfo> SERIAL(destroyFX);
  optional<FXInfo> SERIAL(tryDestroyFX);
  optional<FXInfo> SERIAL(walkOverFX);
  optional<FXInfo> SERIAL(walkIntoFX);
  optional<Sound> SERIAL(walkIntoSound);
  optional<FXVariantName> SERIAL(usageFX);
  optional<Sound> SERIAL(usageSound);
  optional<Sound> SERIAL(destroySound) = Sound(SoundId("REMOVE_CONSTRUCTION"));
  bool SERIAL(hostileSpell) = false;
  optional<FurnitureEffectInfo> SERIAL(lastingEffect);
  optional<FurnitureType> SERIAL(freezeTo);
  struct MeltInfo {
    optional<FurnitureType> SERIAL(meltTo);
    SERIALIZE_ALL(OPTION(meltTo))
  };
  optional<MeltInfo> SERIAL(meltInfo);
  optional<FurnitureType> SERIAL(dissolveTo);
  optional<int> SERIAL(bloodCountdown);
  optional<LocalTime> SERIAL(bloodTime);
  optional<Effect> SERIAL(destroyedEffect);
  optional<Effect> SERIAL(itemsRemovedEffect);
  bool SERIAL(eyeball) = false;
  vector<StorageId> SERIAL(storageIds);
  bool SERIAL(hidesItems) = false;
  optional<ViewId> SERIAL(emptyViewId);
  optional<FurnitureType> SERIAL(diningFurniture);
  optional<CreaturePredicate> SERIAL(usagePredicate);
  optional<AchievementId> SERIAL(minedAchievement);
  bool SERIAL(removeInstantly) = false;
  bool SERIAL(buildingFloor) = false;
  optional<FurnitureType> SERIAL(otherStairs);
};

static_assert(std::is_nothrow_move_constructible<Furniture>::value, "T should be noexcept MoveConstructible");

CEREAL_CLASS_VERSION(Furniture, 2)