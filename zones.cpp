#include "stdafx.h"
#include "zones.h"
#include "position.h"
#include "view_index.h"
#include "movement_type.h"
#include "collective.h"
#include "territory.h"
#include "level.h"
#include "shortest_path.h"

SERIALIZE_DEF(Zones, positions, zones, quarters, quartersSectors)
SERIALIZATION_CONSTRUCTOR_IMPL(Zones)

template <typename T, typename Compare>
vector<const T*> sortQuarters(const vector<T>& v, Compare cmp) {
    vector<const T*> view;
    view.reserve(v.size());
    for (const auto& x : v) view.push_back(&x);
    std::sort(view.begin(), view.end(),
              [&](const T* a, const T* b){ return cmp(*a, *b); });
    return view;
}

bool Zones::isZone(Position pos, ZoneId id) const {
  //PROFILE;
  if (auto z = zones.getReferenceMaybe(pos))
    if (z->contains(id))
      return true;
  return false;
}

bool Zones::isAnyZone(Position pos, EnumSet<ZoneId> id) const {
  //PROFILE;
  if (auto z = zones.getReferenceMaybe(pos))
    return !z->intersection(id).isEmpty();
  return false;
}

void Zones::setZone(Position pos, ZoneId id) {
  PROFILE;
  zones.getOrInit(pos).insert(id);
  positions[id].insert(pos);
  pos.setNeedsRenderAndMemoryUpdate(true);
  if (id == ZoneId::QUARTERS) {
    getOrInitSectors(pos.getLevel()).add(pos.getCoord());
    quartersPositionCache.clear();
  }
}

void Zones::eraseZone(Position pos, ZoneId id) {
  PROFILE;
  zones.getOrInit(pos).erase(id);
  positions[id].erase(pos);
  pos.setNeedsRenderAndMemoryUpdate(true);
  if (id == ZoneId::QUARTERS) {
    getOrInitSectors(pos.getLevel()).remove(pos.getCoord());
    quartersPositionCache.clear();
  }
}

const PositionSet& Zones::getPositions(ZoneId id) const {
  return positions[id];
}

HighlightType getHighlight(ZoneId id) {
  switch (id) {
    case ZoneId::FETCH_ITEMS:
      return HighlightType::FETCH_ITEMS;
    case ZoneId::PERMANENT_FETCH_ITEMS:
      return HighlightType::PERMANENT_FETCH_ITEMS;
    case ZoneId::STORAGE_EQUIPMENT:
      return HighlightType::STORAGE_EQUIPMENT;
    case ZoneId::STORAGE_RESOURCES:
      return HighlightType::STORAGE_RESOURCES;
    case ZoneId::QUARTERS:
      return HighlightType::QUARTERS;
    case ZoneId::LEISURE:
      return HighlightType::LEISURE;
    case ZoneId::GUARD1:
      return HighlightType::GUARD_ZONE1;
    case ZoneId::GUARD2:
      return HighlightType::GUARD_ZONE2;
    case ZoneId::GUARD3:
      return HighlightType::GUARD_ZONE3;
  }
}

void Zones::setHighlights(Position pos, ViewIndex& index) const {
  PROFILE;
  for (auto id : ENUM_ALL(ZoneId))
    if (isZone(pos, id))
      index.setHighlight(getHighlight(id));
}

bool Zones::canSet(Position pos, ZoneId id, const Collective* col) const {
  switch (id) {
    case ZoneId::STORAGE_EQUIPMENT:
    case ZoneId::STORAGE_RESOURCES:
    case ZoneId::GUARD1:
    case ZoneId::GUARD2:
    case ZoneId::GUARD3:
      return pos.canEnterEmpty(MovementTrait::WALK);
    case ZoneId::QUARTERS:
    case ZoneId::LEISURE:
      return col->getTerritory().contains(pos);
    default:
      return true;
  }
}

void Zones::tick() {
  PROFILE_BLOCK("Zones::tick");
  for (auto pos : copyOf(positions[ZoneId::FETCH_ITEMS]))
    if (pos.getItems().empty())
      eraseZone(pos, ZoneId::FETCH_ITEMS);
}

const PositionSet& Zones::getQuartersPositions(Level* level, Sectors::SectorId id) const {
  if (!quartersPositionCache.count(make_pair(level, id))) {
    PositionSet pos;
    if (auto sectors = getReferenceMaybe(quartersSectors, level))
      for (auto v : sectors->getWholeSector(id))
        pos.insert(Position(v, level));
    quartersPositionCache[make_pair(level, id)] = std::move(pos);
  }
  return quartersPositionCache.at(make_pair(level, id));
}

void Zones::assignQuarters(UniqueEntity<Creature>::Id id, Position pos) {
  auto level = pos.getLevel();
  if (!quartersSectors.count(level))
    quartersSectors.emplace(level, Sectors(level->getBounds()));
  auto& sectors = quartersSectors.at(level);
  auto sectorId = *sectors.getSector(pos.getCoord());
  quarters.erase(id);
  quarters.erase(make_pair(level, sectorId));
  quarters.insert(id, make_pair(level, sectorId));
  quartersPositionCache.clear();
}

Sectors& Zones::getOrInitSectors(Level* level) {
  if (!quartersSectors.count(level))
    quartersSectors[level] = Sectors(level->getBounds());
  return quartersSectors.at(level);
}

optional<Zones::QuartersInfo> Zones::getQuartersInfo(Position pos) const {
  if (!pos.isValid())
    return none;
  auto level = pos.getLevel();
  if (auto sectors = getReferenceMaybe(quartersSectors, level)) {
    if (auto sectorId = sectors->getSector(pos.getCoord())) {
      optional<UniqueEntity<Creature>::Id> id;
      if (quarters.contains(make_pair(level, *sectorId)))
        id = quarters.get(make_pair(level, *sectorId));

      return QuartersInfo { id, getQuartersPositions(level, *sectorId)};
    }
  }
  return none;
}

optional<Zones::QuartersInfo> Zones::getQuartersInfoWithLuxury(Position pos) const {
  auto level = pos.getLevel();
  if (auto sectors = getReferenceMaybe(quartersSectors, level)) {
    if (auto sectorId = sectors->getSector(pos.getCoord())) {
      double luxury = 0.0;
      const auto& quartPositions = getQuartersPositions(level, *sectorId);
      for (const auto& p : quartPositions) {
        luxury += p.getTotalLuxuryPlusWalls();
      }
      optional<UniqueEntity<Creature>::Id> id;
      if (quarters.contains(make_pair(level, *sectorId)))
        id = quarters.get(make_pair(level, *sectorId));

      return QuartersInfo { id, quartPositions, luxury };
    }
  }

  return none;
}

const PositionSet& Zones::getQuarters(UniqueEntity<Creature>::Id id) const {
  static const PositionSet empty = PositionSet();
  if (!quarters.contains(id))
    return empty;
  auto res = quarters.get(id);
  return getQuartersPositions(res.first, res.second);
}

optional<double> Zones::getQuartersLuxury(UniqueEntity<Creature>::Id id) const {
  PROFILE;
  auto& quarters = getQuarters(id);
  if (quarters.empty())
    return none;
  double ret = 0;
  for (auto& pos : quarters)
    ret += pos.getTotalLuxuryPlusWalls();
  return ret;
}

optional<UniqueEntity<Creature>::Id> Zones::getAssignedToQuarters(Position pos) const {
  if (auto info = getQuartersInfo(pos))
    return info->id;
  return none;
}

vector<Zones::QuartersInfo> Zones::getAllQuarters(UniqueEntity<Creature>::Id crId) const {
  auto& allQuartersPos = getPositions(ZoneId::QUARTERS);
  optional<QuartersInfo> creaturesQuarters;       
  vector<QuartersInfo> unassignedQuarters;
  vector<QuartersInfo> assignedQuarters;
  vector<QuartersInfo> zoneQrtsList;
  for (const auto& pos : allQuartersPos) {
    if (auto quartInfo = getQuartersInfoWithLuxury(pos)) {
      const PositionSet* posSet = &quartInfo->positions;
      bool found = false;
      for (const auto& zQrts : zoneQrtsList) {
        if (zQrts.positions == *posSet) {
          found = true;
          break;
        }
      }
      if (!found) {
        zoneQrtsList.push_back(*quartInfo);

        if (quartInfo->id){
          if (*quartInfo->id != crId)
            assignedQuarters.push_back(*quartInfo);
          else
            creaturesQuarters.emplace(*quartInfo);
        }
        else {
          unassignedQuarters.push_back(*quartInfo);
        }
      }
    }
  }

  auto sortingFunc = [](const QuartersInfo& a, const QuartersInfo& b) {
    if (a.luxury != b.luxury) return a.luxury > b.luxury;       // desc by luxury
    return a.positions.size() > b.positions.size();             // desc by size
  };

  auto viewUnassigned = sortQuarters(unassignedQuarters, sortingFunc);
  auto viewAssigned = sortQuarters(assignedQuarters, sortingFunc);

  vector<QuartersInfo> finalList;
  finalList.reserve((creaturesQuarters ? 1 : 0)
                   + viewUnassigned.size()
                   + viewAssigned.size());

  if (creaturesQuarters)
    finalList.push_back(*creaturesQuarters);  

  for (const QuartersInfo* p : viewUnassigned) finalList.push_back(*p);
  for (const QuartersInfo* p : viewAssigned)   finalList.push_back(*p);

  return finalList;
}
