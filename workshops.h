#pragma once

#include "resource_id.h"
#include "workshop_type.h"
#include "workshop_array.h"

struct WorkshopItem;
struct WorkshopQueuedItem;
struct WorkshopItemCfg;
class Collective;
class CostInfo;
class ContentFactory;

class Workshops {
  public:
  typedef WorkshopItem Item;
  typedef WorkshopQueuedItem QueuedItem;

  class Type {
    public:
    Type(const vector<Item>& options);
    const vector<Item>& getOptions() const;
    const vector<QueuedItem>& getQueued() const;
    PItem addWork(Collective*, double workAmount, int skillAmount, const optional<ItemPrefix>&);
    void queue(Collective*, int index, int requiredSkill, optional<int> queueIndex = none);
    vector<PItem> unqueue(Collective*, int index);
    void changePriority(Collective* collective, int index, int count, int destIndex);
    void changeNumber(int index, int number);
    bool isIdle(const Collective*, int skillAmount) const;
    void addUpgrade(int index, PItem);
    PItem removeUpgrade(int itemIndex, int runeIndex);
    void updateState(Collective*);

    SERIALIZATION_DECL(Type)

    private:
    friend class Workshops;
    void addDebt(CostInfo);
    void checkDebtConsistency() const;
    vector<Item> SERIAL(options);
    vector<QueuedItem> SERIAL(queued);
    HashMap<CollectiveResourceId, int> SERIAL(debt);
  };

  SERIALIZATION_DECL(Workshops)
  Workshops(WorkshopArray, const ContentFactory*);
  Workshops(const Workshops&) = delete;
  int getDebt(CollectiveResourceId) const;
  vector<WorkshopType> getWorkshopsTypes() const;
  map<WorkshopType, Type> SERIAL(types);
};
