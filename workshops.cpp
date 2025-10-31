#include "stdafx.h"
#include "workshops.h"
#include "collective.h"
#include "item_factory.h"
#include "item.h"
#include "workshop_item.h"
#include "game.h"
#include "view_object.h"
#include "content_factory.h"

Workshops::Workshops(WorkshopArray options, const ContentFactory* factory) {
  for (auto& elem : options)
    types.insert(make_pair(elem.first, Type(elem.second.transform(
        [&](const auto& item){ return item.get(elem.first, factory); }))));
}

SERIALIZATION_CONSTRUCTOR_IMPL(Workshops)
SERIALIZE_DEF(Workshops, types)

Workshops::Type::Type(const vector<Item>& o) : options(o) {}

SERIALIZATION_CONSTRUCTOR_IMPL2(Workshops::Type, Type)
SERIALIZE_DEF(Workshops::Type, options, queued, debt)


const vector<Workshops::Item>& Workshops::Type::getOptions() const {
  return options;
}

void Workshops::Type::addDebt(CostInfo cost) {
  debt[cost.id] += cost.value;
  CHECK(debt[cost.id] >= 0);
}

void Workshops::Type::checkDebtConsistency() const {
  HashMap<CollectiveResourceId, int> nowDebt;
  for (auto& elem : queued) {
    if (!elem.paid)
      nowDebt[options[elem.indexInWorkshop].cost.id] += options[elem.indexInWorkshop].cost.value;
  }
  for (auto& elem : nowDebt)
    CHECK(getValueMaybe(debt, elem.first).value_or(0) == elem.second);
  for (auto& elem : debt)
    CHECK(getValueMaybe(nowDebt, elem.first).value_or(0) == elem.second);
}

void Workshops::Type::queue(Collective* collective, int index, int requiredSkill, optional<int> queueIndex) {
  addDebt(options[index].cost);
  queued.insert(queueIndex.value_or(queued.size()), QueuedItem(options[index], index, false, requiredSkill));
  updateState(collective);
}

// Changes the priority of queued items by moving a block within the queue.
// firstIdx: starting index of the block to move
// middleIdx: ending index (exclusive) of the block to move
// lastIdx: target position where the block should be inserted
void Workshops::Type::changePriority(Collective* collective, int firstIdx, int middleIdx, int lastIdx) {
  moveBlock(queued, firstIdx, middleIdx, lastIdx);
  updateState(collective);
}

void Workshops::Type::updateState(Collective* collective) {
  for (auto& elem : queued)
    if (!elem.paid && collective->hasResource(elem.item.cost)) {
      elem.paid = true;
      collective->takeResource(elem.item.cost);
      addDebt(-elem.item.cost);
    }
}

vector<PItem> Workshops::Type::unqueue(Collective* collective, int index) {
  if (index >= 0 && index < queued.size()) {
    auto& elem = queued[index];
    if (elem.paid) {
      if (elem.state == 0)
        collective->returnResource(elem.item.cost);
    } else
      addDebt(-elem.item.cost);
    auto ret = queued.removeIndexPreserveOrder(index).runes;
    updateState(collective);
    return ret;
  }
  return {};
}

static const double prodMult = 0.15;

static bool canCraft(const Workshops::QueuedItem& product, const Collective* collective, int skillAmount) {
  return (product.paid || collective->hasResource(product.item.cost)) &&
      (!product.item.requiresUpgrades || !product.runes.empty()) && skillAmount >= product.minSkill;
}

bool Workshops::Type::isIdle(const Collective* collective, int skillAmount) const {
  for (auto& product : queued)
    if (canCraft(product, collective, skillAmount))
      return false;
  return true;
}

void Workshops::Type::addUpgrade(int index, PItem rune) {
  queued[index].runes.push_back(std::move(rune));
  sort(queued[index].runes.begin(), queued[index].runes.end(), [](const auto& rune1, const auto& rune2) {
    return make_pair(rune1->getName(), rune1->getViewObject().id()) < make_pair(rune2->getName(), rune2->getViewObject().id());
  });
  checkDebtConsistency();
}

PItem Workshops::Type::removeUpgrade(int itemIndex, int runeIndex) {
  auto ret = queued[itemIndex].runes.removeIndexPreserveOrder(runeIndex);
  return ret;
}

PItem Workshops::Type::addWork(Collective* collective, double amount, int skillAmount,
    const optional<ItemPrefix>& prefix) {
  for (int productIndex : All(queued)) {
    auto& product = queued[productIndex];
    if (canCraft(product, collective, skillAmount)) {
      if (!product.paid) {
        collective->takeResource(product.item.cost);
        addDebt(-product.item.cost);
        product.paid = true;
      }
      auto workDone = amount * prodMult / product.item.workNeeded;
      product.state += workDone;
      if (product.state > 1)
        workDone -= product.state - 1;
      if (product.state >= 1) {
        auto factory = collective->getGame()->getContentFactory();
        auto ret = product.item.type.get(factory);
        if (prefix)
          ret->applyPrefix(*prefix, factory);
        ret->upgrade(std::move(product.runes), factory);
        queued.removeIndexPreserveOrder(productIndex);
        checkDebtConsistency();
        return ret;
      }
      break;
    }
  }
  checkDebtConsistency();
  return nullptr;
}

const vector<Workshops::QueuedItem>& Workshops::Type::getQueued() const {
  return queued;
}

int Workshops::getDebt(CollectiveResourceId resource) const {
  int ret = 0;
  for (auto& elem : types)
    ret += getValueMaybe(elem.second.debt, resource).value_or(0);
  return ret;
}

vector<WorkshopType> Workshops::getWorkshopsTypes() const {
  return getKeys(types);
}
