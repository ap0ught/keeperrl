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

#include "enums.h"
#include "stdafx.h"

#include "player.h"
#include "level.h"
#include "item.h"
#include "name_generator.h"
#include "game.h"
#include "options.h"
#include "creature.h"
#include "item_factory.h"
#include "effect.h"
#include "view_id.h"
#include "map_memory.h"
#include "player_message.h"
#include "item_action.h"
#include "game_info.h"
#include "equipment.h"
#include "spell.h"
#include "spell_map.h"
#include "creature_name.h"
#include "view.h"
#include "view_index.h"
#include "music.h"
#include "model.h"
#include "collective.h"
#include "territory.h"
#include "creature_attributes.h"
#include "attack_level.h"
#include "villain_type.h"
#include "visibility_map.h"
#include "collective_name.h"
#include "view_object.h"
#include "body.h"
#include "furniture_usage.h"
#include "furniture.h"
#include "creature_debt.h"
#include "tutorial.h"
#include "message_generator.h"
#include "message_buffer.h"
#include "pretty_printing.h"
#include "item_type.h"
#include "creature_group.h"
#include "time_queue.h"
#include "unknown_locations.h"
#include "furniture_click.h"
#include "navigation_flags.h"
#include "vision.h"
#include "game_event.h"
#include "view_object_action.h"
#include "dungeon_level.h"
#include "fx_name.h"
#include "furniture_factory.h"
#include "content_factory.h"
#include "target_type.h"
#include "shortest_path.h"
#include "item_types.h"
#include "item_attributes.h"
#include "keybinding.h"
#include "task.h"
#include "collective_control.h"
#include "scripted_ui_data.h"

template <class Archive>
void Player::serialize(Archive& ar, const unsigned int) {
  ar& SUBCLASS(Controller) & SUBCLASS(EventListener);
  ar(travelDir, target, levelMemory, messageBuffer);
  ar(visibilityMap, tutorial, unknownLocations, avatarLevel);
}

SERIALIZABLE(Player)

SERIALIZATION_CONSTRUCTOR_IMPL(Player)

Player::Player(Creature* c, SMapMemory memory, SMessageBuffer buf, SVisibilityMap v, SUnknownLocations loc, STutorial t)
    : Controller(c), levelMemory(memory), messageBuffer(buf), visibilityMap(v), unknownLocations(loc), tutorial(t) {
  visibilityMap->update(c, c->getVisibleTiles());
}

Player::~Player() {
}

void Player::onEvent(const GameEvent& event) {
  using namespace EventInfo;
  auto factory = getGame()->getContentFactory();
  event.visit<void>(
      [&](const Projectile& info) {
        if (teamCanSeeAndSameLevel(info.begin) || teamCanSeeAndSameLevel(info.end))
          getView()->animateObject(info.begin.getCoord(), info.end.getCoord(), info.viewId, info.fx);
        if (info.sound)
          getView()->addSound(*info.sound);
      },
      [&](const CreatureKilled& info) {
        auto pos = info.victim->getPosition();
        if (teamCanSeeAndSameLevel(pos))
          if (auto anim = info.victim->getBody().getDeathAnimation(factory))
            getView()->animation(pos.getCoord(), *anim);
      },
      [&](const CreatureAttacked& info) {
        auto pos = info.victim->getPosition();
        if (teamCanSeeAndSameLevel(pos)) {
          auto dir = info.attacker->getPosition().getDir(pos);
          if (dir.length8() == 1) {
            auto orientation = dir.getCardinalDir();
            if (info.damageAttr == AttrType("DAMAGE"))
              getView()->animation(pos.getCoord(), AnimationId::ATTACK, orientation);
            else if (auto& fx = factory->attrInfo.at(info.damageAttr).meleeFX)
              getView()->animation(FXSpawnInfo(*fx, pos.getCoord(), Vec2(0, 0)));
          }
        }
      },
      [&](const Alarm& info) {
        if (!info.silent) {
          Position pos = info.pos;
          Position myPos = creature->getPosition();
          if (pos == myPos)
            privateMessage(TStringId("AN_ALARM_SOUNDS_NEAR_YOU"));
          else if (pos.isSameLevel(myPos))
            privateMessage(TSentence("AN_ALARM_SOUNDS_IN_THE",
                getCardinalName(myPos.getDir(pos).getBearing().getCardinalDir())));
        }
      },
      [&](const FX& info) {
        if (teamCanSeeAndSameLevel(info.position)) {
          auto fx = FXSpawnInfo(info.fx, info.position.getCoord(), info.direction.value_or(Vec2(0, 0)));
          if (info.fx.name == FXName::TELEPORT_IN)
            deferredAnimations.push_back(std::move(fx));
          else
            getView()->animation(std::move(fx));
        }
      },
      [&](const auto&) {}
  );
}

void Player::pickUpItemAction(int numStack, bool multi) {
  PROFILE;
  CHECK(numStack >= 0);
  auto stacks = creature->stackItems(creature->getPickUpOptions());
  if (getUsableUsageType()) {
    --numStack;
    if (numStack == -1) {
      creature->applySquare(creature->getPosition(), FurnitureLayer::MIDDLE).perform(creature);
      return;
    }
  }
  if (numStack < stacks.size()) {
    vector<Item*> items = stacks[numStack];
    if (multi && items.size() > 1) {
      auto num = getView()->getNumber(TSentence("PICK_UP_HOW_MANY", items[0]->getName(true)),
          Range(1, items.size()), 1);
      if (!num)
        return;
      items = items.getPrefix(*num);
    }
    tryToPerform(creature->pickUp(items));
  }
}

bool Player::tryToPerform(CreatureAction action) {
  if (action)
    action.perform(creature);
  else if (auto reason = action.getFailedReason())
    privateMessage(std::move(*reason));
  return !!action;
}

void Player::applyItem(vector<Item*> items) {
  PROFILE;
  if (items[0]->getApplyTime() > 1_visible) {
    for (const Creature* c : creature->getVisibleEnemies())
      if (creature->getPosition().dist8(c->getPosition()).value_or(3) < 3) {
        if (!getView()->yesOrNoPrompt(TSentence("APPLYING_ITEM_TAKES_X_TURNS", items[0]->getAName(),
            TString(items[0]->getApplyTime()))))
          return;
        else
          break;
      }
  }
  tryToPerform(creature->applyItem(items[0]));
}

optional<Vec2> Player::chooseDirection(const TString& s) {
  return getView()->chooseDirection(creature->getPosition().getCoord(), s);
}

Player::TargetResult Player::chooseTarget(Table<PassableInfo> passable, TargetType type, const TString& s,
    optional<Keybinding> keybinding) {
  return getView()->chooseTarget(creature->getPosition().getCoord(), type, std::move(passable), s, keybinding).visit(
      [&](auto e) -> TargetResult { return e; },
      [&](Vec2 v) -> TargetResult { return Position(v, getLevel()); });
}

void Player::throwItem(Item* item, optional<Position> target) {
  if (!target) {
    if (auto testAction = creature->throwItem(item, creature->getPosition().plus(Vec2(1, 0)), false)) {
      Vec2 origin = creature->getPosition().getCoord();
      Table<PassableInfo> passable(Rectangle::centered(origin, *creature->getThrowDistance(item)),
          PassableInfo::PASSABLE);
      for (auto v : passable.getBounds()) {
        Position pos(v, getLevel());
        if (!creature->canSee(pos) && !getMemory().getViewIndex(pos))
          passable[v] = PassableInfo::UNKNOWN;
        else if (pos.stopsProjectiles(creature->getVision().getId()))
          passable[v] = PassableInfo::NON_PASSABLE;
        else if (pos.getCreature())
          passable[v] = PassableInfo::STOPS_HERE;
      }
      target = chooseTarget(std::move(passable), TargetType::TRAJECTORY, TStringId("WHICH_DIRECTION_THROW"), none)
          .getValueMaybe<Position>();
    } else if (auto reason = testAction.getFailedReason())
      privateMessage(*reason);
    if (!target)
      return;
  }
  tryToPerform(creature->throwItem(item, *target, item->effectAppliedWhenThrown() &&
      item->getEffect()->shouldAIApply(creature, *target) >= 0));
}

void Player::handleIntrinsicAttacks(const vector<UniqueEntity<Item>::Id>& itemIds, ItemAction action) {
  auto& attacks = creature->getBody().getIntrinsicAttacks();
  for (auto part : ENUM_ALL(BodyPart))
    for (auto& attack : attacks[part])
      if (itemIds.contains(attack.item->getUniqueId()))
        switch (action) {
          case ItemAction::INTRINSIC_ACTIVATE:
            attack.active = true;
            break;
          case ItemAction::INTRINSIC_DEACTIVATE:
            attack.active = false;
            break;
          default:
            FATAL << "Unhandled intrinsic item action: " << (int) action;
            break;
        }
}

void Player::handleItems(const vector<UniqueEntity<Item>::Id>& itemIds1, ItemAction action) {
  PROFILE;
  HashSet<Item::Id> itemIds(itemIds1.begin(), itemIds1.end());
  vector<Item*> items = creature->getEquipment().getItems().filter(
      [&](const Item* it) { return itemIds.count(it->getUniqueId());});
  //CHECK(items.size() == itemIds.size()) << int(items.size()) << " " << int(itemIds.size());
  // the above assertion fails for unknown reason, so just fail this softly.
  if (items.empty() || (items.size() == 1 && action == ItemAction::DROP_MULTI))
    return;
  switch (action) {
    case ItemAction::DROP: tryToPerform(creature->drop(items)); break;
    case ItemAction::DROP_MULTI:
      if (auto num = getView()->getNumber(TSentence("DROP_HOW_MANY", items[0]->getName(true)),
          Range(1, items.size()), 1))
        tryToPerform(creature->drop(items.getPrefix(*num)));
      break;
    case ItemAction::THROW: throwItem(items[0]); break;
    case ItemAction::APPLY: applyItem(items); break;
    case ItemAction::UNEQUIP: tryToPerform(creature->unequip(items[0])); break;
    case ItemAction::GIVE: giveAction(items); break;
    case ItemAction::PAY: payForItemAction(items); break;
    case ItemAction::EQUIP: {
      vector<Item*> conflictingItems;
      for (auto it : creature->getEquipment().getAllEquipped())
        if (it->isConflictingEquipment(items[0]))
          conflictingItems.push_back(it);
      if (getView()->confirmConflictingItems(getGame()->getContentFactory(), conflictingItems))
        tryToPerform(creature->equip(items[0]));
      break;
    }
    case ItemAction::NAME:
      if (auto name = getView()->getText(TSentence("ENTER_A_NAME_FOR", items[0]->getTheName()),
          ""_s, 14))
        items[0]->setArtifactName(*name);
      break;
    default: FATAL << "Unhandled item action " << int(action);
  }
}

void Player::hideAction() {
  tryToPerform(creature->hide());
}

bool Player::interruptedByEnemy() {
  if (auto combatIntent = creature->getLastCombatIntent())
    if (combatIntent->isHostile() &&
        combatIntent->time > lastEnemyInterruption.value_or(GlobalTime(-1)) &&
        combatIntent->time > getGame()->getGlobalTime() - 5_visible) {
      lastEnemyInterruption = combatIntent->time;
      privateMessage(TSentence("YOU_ARE_BEING_ATTACKED_BY", combatIntent->attacker->getName().a()));
      return true;
    }
  return false;
}

void Player::targetAction() {
  CHECK(target);
  if (creature->getPosition() == *target || getView()->travelInterrupt()) {
    target = none;
    return;
  }
  if (auto action = creature->moveTowards(*target, NavigationFlags().noDestroying()))
    action.perform(creature);
  else
    target = none;
  if (interruptedByEnemy() || !isTravelEnabled())
    target = none;
}

void Player::payForItemAction(const vector<Item*>& items) {
  int totalPrice = std::accumulate(items.begin(), items.end(), 0,
      [](int sum, const Item* it) { return sum + it->getPrice(); });
  for (auto item : items)
    CHECK(item->getShopkeeper(creature));
  vector<Item*> gold = creature->getGold(totalPrice);
  int canPayFor = [&] {
    int res = 0;
    int remaining = gold.size();
    for (auto it : items)
      if (it->getPrice() <= remaining) {
        ++res;
        remaining -= it->getPrice();
      } else
        break;
    return res;
  }();
  if (canPayFor == 0)
    privateMessage(TStringId("YOU_DONT_HAVE_ENOUGH_GOLD"));
  else if (canPayFor == items.size() || getView()->yesOrNoPrompt(TSentence("YOU_ONLY_HAVE_ENOUGH_FOR",
      TString(canPayFor), items[0]->getName(canPayFor > 1, creature))))
    tryToPerform(creature->payFor(items.getPrefix(canPayFor)));
}

void Player::payForAllItemsAction() {
  if (int totalDebt = creature->getDebt().getTotal(creature)) {
    auto gold = creature->getGold(totalDebt);
    if (gold.size() < totalDebt)
      privateMessage(TStringId("YOU_DONT_HAVE_ENOUGH_GOLD_FOR_EVERYTHING"));
    else
      for (Creature* c : creature->getDebt().getCreditors(creature)) {
        auto debt = creature->getDebt().getAmountOwed(c);
        if (getView()->yesOrNoPrompt(TSentence("GIVE_GOLD_TO", c->getName().title(), TString(debt))))
          tryToPerform(creature->give(c, creature->getGold(debt)));
      }
  }
}

void Player::giveAction(vector<Item*> items) {
  PROFILE;
  if (items.size() > 1) {
    if (auto num = getView()->getNumber(TSentence("GIVE_HOW_MANY", items[0]->getName(true)), Range(1, items.size()), 1))
      items = items.getPrefix(*num);
    else
      return;
  }
  vector<Creature*> creatures;
  for (Position pos : creature->getPosition().neighbors8())
    if (Creature* c = pos.getCreature())
      creatures.push_back(c);
  if (creatures.size() == 1 && getView()->yesOrNoPrompt(TSentence("GIVE_ITEM_TO", items[0]->getTheName(items.size() > 1),
        creatures[0]->getName().the())))
    tryToPerform(creature->give(creatures[0], items));
  else if (auto dir = chooseDirection(TStringId("GIVE_WHOM")))
    if (Creature* whom = creature->getPosition().plus(*dir).getCreature())
      tryToPerform(creature->give(whom, items));
}

void Player::chatAction(optional<Vec2> dir) {
  PROFILE;
  vector<Creature*> creatures;
  for (Position pos : creature->getPosition().neighbors8())
    if (Creature* c = pos.getCreature())
      creatures.push_back(c);
  if (creatures.size() == 1 && !dir) {
    tryToPerform(creature->chatTo(creatures[0]));
  } else
  if (creatures.size() > 1 || dir) {
    if (!dir)
      dir = chooseDirection(TStringId("WHICH_DIRECTION"));
    if (!dir)
      return;
    if (Creature* c = creature->getPosition().plus(*dir).getCreature())
      tryToPerform(creature->chatTo(c));
  }
}

void Player::mountAction() {
  PROFILE;
  vector<Creature*> creatures;
  for (Position pos : creature->getPosition().neighbors8())
    if (Creature* c = pos.getCreature())
      creatures.push_back(c);
  if (creatures.size() == 1) {
    tryToPerform(creature->mount(creatures[0]));
  } else
  if (creatures.size() > 1) {
    auto dir = chooseDirection(TStringId("WHICH_DIRECTION"));
    if (!dir)
      return;
    if (Creature* c = creature->getPosition().plus(*dir).getCreature())
      tryToPerform(creature->mount(c));
  }
}

void Player::fireAction() {
  for (auto spell : creature->getSpellMap().getAvailable(creature))
    if (creature->isReady(spell) && spell->getRange() > 1 && !spell->isEndOnly()) {
      highlightedSpell = spell->getId();
      getView()->updateView(this, false);
      Vec2 origin = creature->getPosition().getCoord();
      Table<PassableInfo> passable(Rectangle::centered(origin, spell->getRange()), PassableInfo::PASSABLE);
      for (auto v : passable.getBounds()) {
        Position pos(v, getLevel());
        if (!creature->canSee(pos) && !getMemory().getViewIndex(pos))
          passable[v] = PassableInfo::UNKNOWN;
        if (spell->isBlockedBy(creature, pos))
          passable[v] = PassableInfo::STOPS_HERE;
      }
      auto res = chooseTarget(std::move(passable), spell->isEndOnly() ? TargetType::POSITION : TargetType::TRAJECTORY,
          TStringId("WHICH_DIRECTION"), Keybinding("FIRE_PROJECTILE"));
      if (res.contains<none_t>())
        break;
      if (auto pos = res.getValueMaybe<Position>()) {
        tryToCast(spell, *pos);
        break;
      }
      if (res.getValueMaybe<Keybinding>())
        continue;
    }
  highlightedSpell = none;
}

void Player::tryToCast(const Spell* spell, Position target) {
  if (spell->isFriendlyFire(creature, target)) {
    if (creature->isAffected(LastingEffect::PEACEFULNESS)) {
      creature->privateMessage(TStringId("CANT_ATTACK_WHILE_PEACEFUL"));
      return;
    }
    optional<int> res = 0;
    if (friendlyFireWarningCooldown.value_or(-10000_global) < getGame()->getGlobalTime())
      res = getView()->multiChoice(TStringId("MOVE_MAY_HARM_ALLIES"), {
          TStringId("YES"), TStringId("NO"), TStringId("YES_AND_DONT_ASK_FOR_50_TURNS")
      });
    if (res == 2)
      friendlyFireWarningCooldown = getGame()->getGlobalTime() + 50_visible;
    if (!res || res == 1)
      return;
  }
  tryToPerform(creature->castSpell(spell, target));
}

void Player::spellAction(int id) {
  auto available = creature->getSpellMap().getAvailable(creature);
  if (id >= 0 && id < available.size()) {
    auto spell = available[id];
    int range = spell->getRange();
    if (range == 0)
      tryToPerform(creature->castSpell(spell));
    else {
      Vec2 origin = creature->getPosition().getCoord();
      Table<PassableInfo> passable(Rectangle::centered(origin, range), PassableInfo::PASSABLE);
      for (auto v : passable.getBounds()) {
        Position pos(v, getLevel());
        if (!creature->canSee(pos) && !getMemory().getViewIndex(pos))
          passable[v] = PassableInfo::UNKNOWN;
        if (spell->isBlockedBy(creature, pos))
          passable[v] = PassableInfo::STOPS_HERE;
      }
      if (auto target = chooseTarget(std::move(passable), spell->isEndOnly() ? TargetType::POSITION : TargetType::TRAJECTORY,
          TStringId("WHICH_DIRECTION"), none).getValueMaybe<Position>())
        tryToCast(spell, *target);
    }
  }
}

const MapMemory& Player::getMemory() const {
  return *levelMemory;
}

void Player::sleeping() {
  PROFILE;
  MEASURE(
      getView()->updateView(this, false),
      "level render time");
  if (LastingEffects::losesControl(creature,
      creature->getPosition().getModel() == getGame()->getMainModel().get()))
    onLostControl();
  // Pop all queued actions and discard them
  do {
    if (getView()->getAction().getId() == UserInputId::IDLE)
      break;
  } while(true);
}

vector<Player::OtherCreatureCommand> Player::getOtherCreatureCommands(Creature* c) const {
  vector<OtherCreatureCommand> ret;
  auto genAction = [&](int priority, ViewObjectAction name, bool allowAuto, CreatureAction action) {
    if (action)
      ret.push_back({priority, name, allowAuto, [action](Player* player) { player->tryToPerform(action); }});
  };
  if (c->getPosition().dist8(creature->getPosition()) == 1) {
    genAction(0, ViewObjectAction::SWAP_POSITION, true, creature->move(c->getPosition(), none));
    genAction(3, ViewObjectAction::PUSH, true, creature->push(c));
  }
  if (creature->isEnemy(c)) {
    genAction(1, ViewObjectAction::ATTACK, true, creature->attack(c));
  } else {
    if (!creature->isAffected(LastingEffect::PEACEFULNESS))
      genAction(1, ViewObjectAction::ATTACK, false, creature->attack(c));
    genAction(1, ViewObjectAction::PET, false, creature->pet(c));
    genAction(1, ViewObjectAction::MOUNT, false, creature->mount(c));
  }
  if (creature == c) {
    genAction(0, ViewObjectAction::SKIP_TURN, true, creature->wait());
    genAction(1, ViewObjectAction::DISMOUNT, false, creature->dismount());
    if (auto steed = creature->getSteed()) {
      genAction(10, ViewObjectAction::CHAT, false, creature->chatTo(steed));
      genAction(10, ViewObjectAction::PET, false, creature->pet(steed));
    }
  }
  genAction(10, ViewObjectAction::CHAT, false, creature->chatTo(c));
  std::stable_sort(ret.begin(), ret.end(), [](const auto& c1, const auto& c2) {
    if (c1.allowAuto && !c2.allowAuto)
      return true;
    if (c2.allowAuto && !c1.allowAuto)
      return false;
    return c1.priority < c2.priority;
  });
  return ret;
}

void Player::creatureClickAction(Position pos, bool extended) {
  if (auto clicked = pos.getCreature()) {
    auto commands = getOtherCreatureCommands(clicked);
    if (extended) {
      auto commandsText = commands.transform([](auto& command) -> TString { return getText(command.name); });
      if (auto index = getView()->chooseAtMouse(commandsText))
        commands[*index].perform(this);
    }
    else if (!commands.empty() && commands[0].allowAuto)
      commands[0].perform(this);
  } else
  if (auto furniture = pos.getFurniture(FurnitureLayer::MIDDLE))
    if (furniture->getClickType() && furniture->getTribe() == creature->getTribeId()) {
      furniture->click(pos);
      updateSquareMemory(pos);
    }
}

void Player::retireMessages() {
  auto& messages = messageBuffer->current;
  if (!messages.empty()) {
    messages = {messages.back()};
    messages[0].setFreshness(0);
  }
}

vector<Player::CommandInfo> Player::getCommands() const {
  PROFILE;
  bool canChat = false;
  for (Position pos : creature->getPosition().neighbors8())
    if (Creature* c = pos.getCreature())
      canChat = true;
  bool canMount = false;
  if (creature->isAffected(LastingEffect::RIDER))
    for (Position pos : creature->getPosition().neighbors8())
      if (Creature* c = pos.getCreature())
        if (!!creature->mount(c))
          canMount = true;
  return {
    {PlayerInfo::CommandInfo{TStringId("FIRE_RANGED_WEAPON_OR_SPELL"), Keybinding("FIRE_PROJECTILE"), TString(), true},
      [] (Player* player) { player->fireAction(); }, false},
    {PlayerInfo::CommandInfo{TStringId("SKIP_TURN_ACTION"), Keybinding("SKIP_TURN"), TString(), true},
      [] (Player* player) { player->tryToPerform(player->creature->wait()); }, false},
    {PlayerInfo::CommandInfo{TStringId("WAIT_COMMAND_NAME"), Keybinding("WAIT"),
        TStringId("WAIT_COMMAND_DESCRIPTION"),
        getGame()->getPlayerCreatures().size() > 1},
      [] (Player* player) {
          player->getModel()->getTimeQueue().postponeMove(player->creature);
          player->creature->getPosition().setNeedsRenderUpdate(true); }, false},
    /*{PlayerInfo::CommandInfo{"Travel", 't', "Travel to another site.", !getGame()->isSingleModel()},
      [] (Player* player) { player->getGame()->transferAction(player->getTeam()); }, false},*/
    creature->getSteed()
      ? Player::CommandInfo{PlayerInfo::CommandInfo{TStringId("DISMOUNT_COMMAND_NAME"), Keybinding("MOUNT"),
          TStringId("DISMOUNT_COMMAND_DESCRIPTION"), true},
            [] (Player* player) { player->tryToPerform(player->creature->dismount()); }, false}
      : Player::CommandInfo{PlayerInfo::CommandInfo{TStringId("MOUNT_ACTION"), Keybinding("MOUNT"),
          TStringId("DISMOUNT_COMMAND_DESCRIPTION"), canMount},
            [] (Player* player) { player->mountAction(); }, false},
    Player::CommandInfo{PlayerInfo::CommandInfo{TStringId("CHAT_ACTION"), Keybinding("CHAT"), TStringId("CHAT_COMMAND_DESCRIPTION"), canChat},
      [] (Player* player) { player->chatAction(); }, false},
    {PlayerInfo::CommandInfo{TStringId("HIDE_COMMAND_NAME"), Keybinding("HIDE"), TStringId("HIDE_COMMAND_DESCRIPTION"),
        !!creature->hide()},
      [] (Player* player) { player->hideAction(); }, false},
    /*{PlayerInfo::CommandInfo{"Pay for all items", 'p', "Pay debt to a shopkeeper.", true},
      [] (Player* player) { player->payForAllItemsAction();}, false},*/
    {PlayerInfo::CommandInfo{TStringId("DROP_EVERYTHING_COMMAND_NAME"), Keybinding("DROP_EVERYTHING"),
        TStringId("DROP_EVERYTHING_COMMAND_DESCRIPTION"), !creature->getEquipment().isEmpty()},
      [] (Player* player) { auto c = player->creature; player->tryToPerform(c->drop(c->getEquipment().getItems())); }, false},
    {PlayerInfo::CommandInfo{TStringId("MESSAGE_HISTORY"), Keybinding("MESSAGE_HISTORY"), TStringId("SHOW_MESSAGE_HISTORY"), true},
      [] (Player* player) { player->showHistory(); }, false},
#ifndef RELEASE
    {PlayerInfo::CommandInfo{TStringId("WAIT_MULTIPLE_TURNS"), none, TString(), true},
      [] (Player* player) {
        if (auto num = player->getView()->getNumber(TStringId("WAIT_HOW_MANY_TURNS"), Range(1, 2000), 30))
          for (int i : Range(*num))
            player->tryToPerform(player->creature->wait());
      }, false},
#endif
  };
}

void Player::updateUnknownLocations() {
  vector<Position> locations;
  for (auto col : getModel()->getCollectives())
    if (!col->isConquered())
      if (auto& pos = col->getTerritory().getCentralPoint())
        if (pos->isSameLevel(getLevel()) && !getMemory().getViewIndex(*pos))
          locations.push_back(*pos);
  unknownLocations->update(locations);
}

void Player::updateSquareMemory(Position pos) {
  ViewIndex index;
  pos.getViewIndex(index, creature);
  levelMemory->update(pos, index);
  if (auto belowPos = pos.getGroundBelow())
    updateSquareMemory(belowPos->first);
}

bool Player::canTravel() const {
  if (!creature->getPosition().getLevel()->canTranfer) {
    getView()->presentText(none, TSentence("YOU_DONT_KNOW_HOW_TO_LEAVE", creature->getName().bare()));
    return false;
  }
  auto team = getTeam();
  for (auto& c : team) {
    if (auto e = LastingEffects::restrictedTravel(c)) {
      getView()->presentText(none, capitalFirst(
          TSentence(team.size() == 1 ? "YOU_CANT_TRAVEL_WHILE" : "CANT_TRAVEL_WHILE",
          c->getName().the(), LastingEffects::getAdjective(*e))));
      return false;
    }
    Creature* attacker = nullptr;
    auto tryAttacker = [&] (Creature* c) {
      if (!team.contains(c) && !LastingEffects::doesntMove(c) && c->getPosition().dist8(creature->getPosition()).value_or(10) < 10)
        attacker = c;
    };
    if (auto intent = c->getLastCombatIntent())
      if (intent->time > *creature->getGlobalTime() - 7_visible && intent->isHostile())
        tryAttacker(intent->attacker);
    if (auto closest = c->getClosestEnemy())
      if (closest->getPosition().dist8(creature->getPosition()).value_or(4) < 4)
        tryAttacker(closest);
    if (attacker) {
      getView()->presentText(none, TSentence("CANT_TRAVEL_WHILE_BEING_ATTACKED_BY", attacker->getName().a()));
      return false;
    }
    for (auto item : c->getEquipment().getItems())
      if (item->getShopkeeper(c)) {
        getView()->presentText(none, TStringId("CANT_TRAVEL_WHILE_CARRYING_UNPAID"));
        return false;
      }
  }
  return true;
}

void Player::makeMove() {
  PROFILE;
  creature->getPosition().setNeedsRenderUpdate(true);
  updateUnknownLocations();
  generateHalluIds();
  if (!isSubscribed())
    subscribeTo(getModel());
  considerKeeperModeTravelMusic();
  for (Position pos : creature->getVisibleTiles())
    updateSquareMemory(pos);
  MEASURE(
      getView()->updateView(this, false),
      "level render time");
  //}
  for (auto& elem : deferredAnimations)
    getView()->animation(std::move(elem));
  deferredAnimations.clear();
  getView()->refreshView();
  /*if (displayTravelInfo && creature->getPosition().getName() == "road"
      && getGame()->getOptions()->getBoolValue(OptionId::HINTS)) {
    getView()->presentText("", "Use ctrl + arrows to travel quickly on roads and corridors.");
    displayTravelInfo = false;
  }*/
  if (getGame()->getOptions()->getBoolValue(OptionId::CONTROLLER_HINT_TURN_BASED)) {
    getView()->scriptedUI("controller_hint_turn_based", ScriptedUIData{});
    getGame()->getOptions()->setValue(OptionId::CONTROLLER_HINT_TURN_BASED, 0);
  }
  UserInput action = getView()->getAction();
  if (target && action.getId() == UserInputId::IDLE)
    targetAction();
  else {
    bool wasJustTravelling = !!target;
    if (action.getId() != UserInputId::IDLE) {
      if (action.getId() != UserInputId::REFRESH) {
        retireMessages();
      }
      if (action.getId() == UserInputId::TILE_CLICK)
        target = none;
    }
    if (!handleUserInput(action))
      switch (action.getId()) {
        case UserInputId::MOVE:
          moveAction(action.get<Vec2>());
          break;
        case UserInputId::TILE_CLICK: {
          Position newPos = creature->getPosition().withCoord(action.get<Vec2>());
          if (newPos.dist8(creature->getPosition()) == 1) {
            Vec2 dir = creature->getPosition().getDir(newPos);
            moveAction(dir);
          }
          else if (newPos != creature->getPosition() && !wasJustTravelling)
            target = newPos;
          break;
        }
        case UserInputId::INVENTORY_ITEM:
          handleItems(action.get<InventoryItemInfo>().items, action.get<InventoryItemInfo>().action);
          break;
        case UserInputId::INTRINSIC_ATTACK:
          handleIntrinsicAttacks(action.get<InventoryItemInfo>().items, action.get<InventoryItemInfo>().action);
          break;
        case UserInputId::PICK_UP_ITEM: pickUpItemAction(action.get<int>()); break;
        case UserInputId::PICK_UP_ITEM_MULTI: pickUpItemAction(action.get<int>(), true); break;
        case UserInputId::CAST_SPELL: spellAction(action.get<int>()); break;
        case UserInputId::CREATURE_MAP_CLICK:
          creatureClickAction(Position(action.get<Vec2>(), getLevel()), false);
          break;
        case UserInputId::CREATURE_MAP_CLICK_EXTENDED:
          creatureClickAction(Position(action.get<Vec2>(), getLevel()), true);
          break;
        case UserInputId::EXIT: getGame()->exitAction(); return;
        case UserInputId::APPLY_EFFECT: {
          Effect effect;
          if (auto error = PrettyPrinting::parseObject(effect, action.get<string>()))
            getView()->presentText(none, TString("Couldn't parse \"" + action.get<string>() + "\": " + *error));
          else
            effect.apply(creature->getPosition(), creature);
          break;
        }
        case UserInputId::CREATE_ITEM: {
          ItemType item;
          if (auto error = PrettyPrinting::parseObject(item, action.get<string>()))
            getView()->presentText(none, TString("Couldn't parse \"" + action.get<string>() + "\": " + *error));
          else
            if (auto cnt = getView()->getNumber(TString("Enter number of items"_s), Range(1, 1000), 1))
              creature->take(item/*.setPrefixChance(1)*/.get(*cnt, getGame()->getContentFactory()));
          break;
        }
        case UserInputId::SUMMON_ENEMY: {
          CreatureId id;
          if (auto error = PrettyPrinting::parseObject(id, action.get<string>()))
            getView()->presentText(none, TString("Couldn't parse \"" + action.get<string>() + "\": " + *error));
          else {
            auto factory = CreatureGroup::singleType(TribeId::getMonster(), id);
            Effect::summon(creature->getPosition(), factory, 1, 1000_visible,
                3_visible);
          }
          break;
        }
        case UserInputId::PLAYER_COMMAND: {
          int index = action.get<int>();
          auto commands = getCommands();
          if (index >= 0 && index < commands.size()) {
            commands[index].perform(this);
            if (commands[index].actionKillsController)
              return;
          }
          break;
        }
        case UserInputId::PAY_DEBT:
          payForAllItemsAction();
          break;
        case UserInputId::TUTORIAL_CONTINUE:
          if (tutorial)
            tutorial->continueTutorial(getGame());
          break;
        case UserInputId::TUTORIAL_GO_BACK:
          if (tutorial)
            tutorial->goBack();
          break;
        case UserInputId::SCROLL_TO_HOME:
          getView()->resetCenter();
          break;
        case UserInputId::DRAW_WORLD_MAP: {
          if (canTravel())
            transferAction();
          break;
        }
        case UserInputId::CREATURE_DRAG_DROP: {
          auto info = action.get<CreatureDropInfo>();
          Position target(info.pos, getLevel());
          for (auto c : getTeam())
            if (c->getUniqueId() == info.creatureId) {
              c->getController()->setDragTask(Task::goTo(target));
            }
          break;
        }
        case UserInputId::SCROLL_STAIRS:
          scrollStairs(action.get<int>());
          break;
    #ifndef RELEASE
        case UserInputId::CHEAT_ATTRIBUTES:
          creature->getAttributes().increaseBaseAttr(AttrType("DAMAGE"), 80);
          creature->getAttributes().increaseBaseAttr(AttrType("DEFENSE"), 80);
          creature->getAttributes().increaseBaseAttr(AttrType("SPELL_DAMAGE"), 80);
          creature->addPermanentEffect(LastingEffect::SPEED);
          creature->addPermanentEffect(LastingEffect::FLYING);
          avatarLevel->increaseLevel();
          break;
    #endif
        default:
          return;
      }
    if (LastingEffects::losesControl(creature,
        creature->getPosition().getModel() == getGame()->getMainModel().get())) {
      onLostControl();
      return;
    }
  }
  getView()->resetCenter();
  creature->getPosition().setNeedsRenderUpdate(true);
}

void Player::transferAction() {
  auto view = getView();
  auto game = getGame();
  auto creatures = getTeam();
  if (auto to = game->chooseSite(creatures[0]->getLevel()->getModel())) {
    creatures = creatures.filter([&](const Creature* c) { return c->getPosition().getModel() != to; });
    vector<PlayerInfo> cant;
    vector<PlayerInfo> turnedOff;
    auto contentFactory = getGame()->getContentFactory();
    for (Creature* c : copyOf(creatures)) {
      if (c->isAffected(LastingEffect::TURNED_OFF)) {
        turnedOff.push_back(PlayerInfo(c, contentFactory));
        creatures.removeElement(c);
      }
      if (!game->canTransferCreature(c, to) || (c->isAffected(LastingEffect::SUNLIGHT_VULNERABLE) &&
            game->getSunlightInfo().getState() == SunlightState::DAY)) {
        cant.push_back(PlayerInfo(c, contentFactory));
        creatures.removeElement(c);
      }
    }
    if (!cant.empty() && !view->creatureInfo(TStringId("MINIONS_LEFT_BEHIND_SUNLIGHT"), true, cant))
      return;
    if (!turnedOff.empty() && !view->creatureInfo(TStringId("MINIONS_LEFT_BEHIND_TURNED_OFF"), true, turnedOff))
      return;
    if (!creatures.empty()) {
      for (Creature* c : creatures) {
        game->transferCreature(c, to);
      }
      game->setWasTransfered();
    }
    forceSteeds();
    for (auto col : to->getCollectives())
      if (!!col->getName() && col->getControl()->considerVillainAmbush(creatures)) {
        view->updateView(this, false);
        game->addAnalytics("ambushed", col->getName()->full.data());
        getView()->windowedMessage({col->getName()->viewId}, TStringId("YOU_HAVE_BEEN_AMBUSHED"));
        break;
      }
  }
}

void Player::showHistory() {
  PlayerMessage::presentMessages(getView(), messageBuffer->history);
}

static optional<TString> getForceMovementQuestion(Position pos, const Creature* creature) {
  if (pos.canEnterEmpty(creature))
    return none;
  else if (pos.isBurning())
    return TString(TStringId("WALKING_INTO_FIRE_WARNING"));
  else if (pos.canEnterEmpty(MovementTrait::SWIM))
    return TString(TStringId("WATER_VERY_DEEP_WARNING"));
  else if (pos.sunlightBurns() && creature->getMovementType().isSunlightVulnerable())
    return TString(TStringId("SUNLIGHT_WARNING"));
  else if (pos.isTribeForbidden(creature->getTribeId()))
    return TString(TStringId("FOBIDDEN_ZONE_WARNING"));
  else
    return TString(TSentence("WALKING_INTO_X_WARNING", pos.getName()));
}

void Player::moveAction(Vec2 dir) {
  auto dirPos = creature->getPosition().plus(dir);
  if (auto steed = creature->getSteed())
    if (auto& a = steed->getAttributes().steedAchievement)
      getGame()->achieve(*a);
  if (tryToPerform(creature->move(dir)))
    return;
  if (auto action = creature->forceMove(dir)) {
    auto nextQuestion = getForceMovementQuestion(dirPos, creature);
    auto hereQuestion = getForceMovementQuestion(creature->getPosition(), creature);
    if (hereQuestion == nextQuestion || getView()->yesOrNoPrompt(*nextQuestion, none, true))
      action.perform(creature);
    return;
  }
  if (auto other = dirPos.getCreature()) {
    auto actions = getOtherCreatureCommands(other);
    if (!actions.empty() && actions[0].allowAuto) {
      actions[0].perform(this);
      return;
    }
    tryToPerform(creature->push(other));
    return;
  }
  if (!dirPos.canEnterEmpty(creature))
    tryToPerform(creature->destroy(dir, DestroyAction::Type::BASH));
  if (!dirPos.isCovered() && dirPos.isUnavailable() && canTravel() && !getGame()->isSingleModel() &&
      creature->getPosition().getLevel() == getModel()->getGroundLevel()) {
    transferAction();
  }
}

bool Player::isPlayer() const {
  return true;
}

void Player::privateMessage(PlayerMessage message) {
  if (View* view = getView()) {
//    if (message.getText().size() < 2)
//      return;
    messageBuffer->history.push_back(message);
    auto& messages = messageBuffer->current;
    if (!messages.empty() && messages.back().getFreshness() < 1)
      messages.clear();
    if (message.getPriority() == MessagePriority::CRITICAL)
      view->presentText(none, message.getText());
    messages.emplace_back(std::move(message));
  }
}

Level* Player::getLevel() const {
  return creature->getLevel();
}

Game* Player::getGame() const {
  if (creature)
    return creature->getGame();
  else
    return nullptr;
}

Model* Player::getModel() const {
  return creature->getLevel()->getModel();
}

View* Player::getView() const {
  if (auto game = getGame())
    return game->getView();
  else
    return nullptr;
}

Vec2 Player::getScrollCoord() const {
  return creature->getPosition().getCoord();
}

bool Player::showScrollCoordOnMinimap() const {
  return true;
}

Level* Player::getCreatureViewLevel() const {
  return creature->getLevel();
}

static MessageGenerator messageGenerator(MessageGenerator::SECOND_PERSON);

MessageGenerator& Player::getMessageGenerator() const {
  return messageGenerator;
}


/* edit distance, doesn't work very well
  static double getScore(const string& target, const string& candidate) {
  int m = target.size();
  int n = candidate.size();
  Table<double> res(Rectangle(-1, -1, m, n), 0);
  for (int i : Range(m))
    res[i][-1] = i + 1;
  for (int i : Range(n))
    res[-1][i] = i + 1;
  for (Vec2 v : Rectangle(0, 0, m, n))
    if (tolower(target[v.x]) == tolower(candidate[v.y]))
      res[v] = res[v - Vec2(1, 1)];
    else
      res[v] = min(min(
          1 + res[v - Vec2(1, 0)],
          0.1 + res[v - Vec2(0, 1)]),
          1.1 + res[v - Vec2(1, 1)]);
  return res[target.size() - 1][candidate.size() - 1];
}*/

// trigrams
static double getScore(string target, string candidate) {
  set<string> ngrams;
  const int n = 3;
  auto addPrefAndSuf = [] (string& s) {
    s = "__ " + s + " __";
  };
  addPrefAndSuf(target);
  addPrefAndSuf(candidate);
  for (int i : Range(target.size() - n + 1))
    ngrams.insert(target.substr(i, n));
  int result = 0;
  for (int i : Range(candidate.size() - n + 1))
    result += ngrams.count(candidate.substr(i, n));
  return result - double(candidate.size()) / 6;
}

struct WishedItemInfo {
  variant<ItemType, CreatureId> type;
  TString name;
  Range count;
};

static vector<WishedItemInfo> getWishedItems(ContentFactory* factory) {
  vector<WishedItemInfo> ret;
  for (auto& creature : factory->getCreatures().getAllCreatures())
    ret.push_back(WishedItemInfo {
      creature,
      factory->getCreatures().fromId(creature, TribeId::getMonster())->getName().bare(),
      Range(1, 2)
    });
  for (auto& elem : factory->items) {
    ret.push_back(WishedItemInfo {
      ItemType(elem.first),
      elem.second->name,
      elem.second->wishedCount
    });
  }
  for (auto effect : ENUM_ALL(LastingEffect))
    if (LastingEffects::canWishFor(effect)) {
      ret.push_back(WishedItemInfo {
        ItemType(ItemTypes::Ring{effect}),
        TSentence("RING_OF", LastingEffects::getName(effect)),
        Range(1, 2)
      });
      ret.push_back(WishedItemInfo {
        ItemType(ItemTypes::Amulet{effect}),
        TSentence("AMULET_OF", LastingEffects::getName(effect)),
        Range(1, 2)
      });
    }
  for (auto& buff : factory->buffs)
    if (buff.second.canWishFor) {
      ret.push_back(WishedItemInfo {
        ItemType(ItemTypes::Ring{buff.first}),
        TSentence("RING_OF", buff.second.name),
        Range(1, 2)
      });
      ret.push_back(WishedItemInfo {
        ItemType(ItemTypes::Amulet{buff.first}),
        TSentence("AMULET_OF", buff.second.name),
        Range(1, 2)
      });
    }
  vector<Effect> allEffects = Effect::getWishedForEffects(factory);
  for (auto& effect : allEffects) {
    ret.push_back(WishedItemInfo {
      ItemType(ItemTypes::Scroll{effect}),
      TSentence("SCROLL_OF", effect.getName(factory)),
      Range(1, 2)
    });
    ret.push_back(WishedItemInfo {
      ItemType(ItemTypes::Potion{effect}),
      TSentence("POTION_OF", effect.getName(factory)),
      Range(1, 2)
    });
    ret.push_back(WishedItemInfo {
      ItemType(ItemTypes::Mushroom{effect}),
      TSentence("MUSHROOM_OF", effect.getName(factory)),
      Range(1, 2)
    });
  }

  return ret;
}

void Player::grantWish(TString message) {
  if (auto text = getView()->getText(message, "", 40)) {
    int count = 1;
    optional<variant<ItemType, CreatureId>> wishType;
    double bestScore = 0;
    for (auto& elem : getWishedItems(getGame()->getContentFactory())) {
      double score = getScore(*text, getView()->translate(elem.name));
      if (score > bestScore || !wishType) {
        bestScore = score;
        wishType = elem.type;
        count = Random.get(elem.count);
      }
    }
    wishType->visit(
        [&](ItemType itemType) {
          auto items = itemType.get(count, getGame()->getContentFactory());
          auto name = items[0]->getPluralAName(items.size());
          getGame()->addAnalytics("wishItem", *text + ":" + getView()->translate(name));
          creature->verb(TStringId("RECEIVE"), TStringId("RECEIVES"), TString(name));
          creature->getEquipment().addItems(std::move(items), creature);
        },
        [&](CreatureId id) {
          auto res = Effect::summon(creature, id, 1, 100_visible);
          if (!res.empty()) {
            getGame()->addAnalytics("wishCreature", *text + ":" + res[0]->identify());
            creature->verb(TStringId("YOU_HAVE_SUMMONED"), TStringId("HAS_SUMMONED"), res[0]->getName().a());
          }
        }
    );
  }
}

bool Player::teamCanSeeAndSameLevel(Position pos) const {
  return pos.isSameLevel(creature->getPosition()) &&
      (visibilityMap->isVisible(pos) || getGame()->getOptions()->getBoolValue(OptionId::SHOW_MAP));
}

void Player::getViewIndex(Vec2 pos, ViewIndex& index) const {
  bool canSee = teamCanSeeAndSameLevel(Position(pos, getLevel()));
  Position position = creature->getPosition().withCoord(pos);
  if (auto belowPos = position.getGroundBelow()) {
    if (auto memIndex = getMemory().getViewIndex(belowPos->first))
      index.mergeGroundBelow(*memIndex, belowPos->second);
    return;
  }
  if (canSee)
    position.getViewIndex(index, creature);
  else
    index.setHiddenId(position.getTopViewId());
  if (!canSee)
    if (auto memIndex = getMemory().getViewIndex(position))
      index.mergeFromMemory(*memIndex);
  if (position.isTribeForbidden(creature->getTribeId()))
    index.setHighlight(HighlightType::FORBIDDEN_ZONE);
#ifndef RELEASE
  /*if (getGame()->getOptions()->getBoolValue(OptionId::SHOW_MAP))
    for (auto col : getGame()->getCollectives())
      if (col->getTerritory().contains(position))
        index.setHighlight(HighlightType::RECT_SELECTION);*/
#endif
  if (auto furniture = position.getFurniture(FurnitureLayer::MIDDLE)) {
    if (auto clickType = furniture->getClickType())
      if (furniture->getTribe() == creature->getTribeId())
        if (auto& obj = furniture->getViewObject())
          if (index.hasObject(obj->layer()))
            index.getObject(obj->layer()).setExtendedActions({FurnitureClick::getClickAction(*clickType, position, furniture)});
  }
  if (Creature* c = position.getCreature()) {
    auto globalTime = *c->getGlobalTime();
    if ((canSee && creature->canSeeInPosition(c, globalTime)) || c == creature ||
        creature->canSeeOutsidePosition(c, globalTime)) {
      index.insert(c->getViewObjectFor(creature->getTribe()));
      index.modEquipmentCounts() = c->getEquipment().getCounts();
      if (auto steed = c->getSteed()) {
        index.getObject(ViewLayer::CREATURE).setModifier(ViewObjectModifier::RIDER);
        auto obj = steed->getViewObjectFor(creature->getTribe());
        obj.getCreatureStatus().intersectWith(getDisplayedOnMinions());
        obj.setLayer(ViewLayer::STEED);
        index.insert(std::move(obj));
      }
      auto& object = index.getObject(ViewLayer::CREATURE);
      if (c == creature)
        object.setModifier(getGame()->getPlayerCreatures().size() == 1
             ? ViewObject::Modifier::PLAYER : ViewObject::Modifier::PLAYER_BLINK);
      if (getTeam().contains(c))
        object.setModifier(ViewObject::Modifier::TEAM_HIGHLIGHT);
      if (creature->isEnemy(c))
        object.setModifier(ViewObject::Modifier::HOSTILE);
      else
        object.getCreatureStatus().intersectWith(getDisplayedOnMinions());
      auto actions = getOtherCreatureCommands(c);
      if (!actions.empty()) {
        if (actions[0].allowAuto)
          object.setClickAction(actions[0].name);
        EnumSet<ViewObjectAction> extended;
        for (auto& action : actions)
          if (action.name != object.getClickAction())
            extended.insert(action.name);
        object.setExtendedActions(extended);
      }
    } else if (creature->isUnknownAttacker(c))
      index.insert(ViewObject(ViewId("unknown_monster"), ViewLayer::CREATURE));
  }
  if (unknownLocations->contains(position))
    index.insert(ViewObject(ViewId("unknown_monster"), ViewLayer::TORCH2, TStringId("SURPRISE")));
  if (position != creature->getPosition() && creature->isAffected(LastingEffect::HALLU))
    for (auto& object : index.getAllObjects())
      object.setId(shuffleViewId(object.getViewIdList()));
}

ViewIdList Player::shuffleViewId(const ViewIdList& id) const {
  for (auto& ids : halluIds)
    if (ids.count(id))
      return Random.choose(ids);
  return id;
}

void Player::generateHalluIds() {
  if (halluIds.empty()) {
    halluIds.emplace_back();
    for (auto& id : getGame()->getContentFactory()->getCreatures().getAllCreatures())
      halluIds.back().insert(getGame()->getContentFactory()->getCreatures().getViewId(id));
    /*for (auto type : ENUM_ALL(FurnitureType)) {
      auto f = FurnitureFactory::get(type, creature->getTribeId());
      if (f->getLayer() == FurnitureLayer::MIDDLE && !f->isWall())
        if (auto& obj = f->getViewObject())
          halluIds.back().insert(obj->id());
    }*/

  }
}

void Player::onKilled(Creature* attacker) {
  unsubscribe();
  getView()->updateView(this, false);
  auto game = getGame();
  if (game->getPlayerCreatures().size() == 1 && getView()->yesOrNoPrompt(TStringId("DISPLAY_MESSAGE_HISTORY_PROMPT"),
      ViewIdList{ViewId("grave")}, false,
      "yes_or_no_below"))
    showHistory();
}

void Player::onLostControl() {
}

vector<Creature*> Player::getTeam() const {
  return {creature};
}

void Player::forceSteeds() const {
}

bool Player::isTravelEnabled() const {
  return true;
}

bool Player::handleUserInput(UserInput) {
  return false;
}

optional<FurnitureUsageType> Player::getUsableUsageType() const {
  if (auto furniture = creature->getPosition().getFurniture(FurnitureLayer::MIDDLE))
    if (furniture->canUse(creature))
      if (auto usageType = furniture->getUsageType())
        if (!FurnitureUsage::getUsageQuestion(*usageType, furniture->getName()).empty())
          return usageType;
  return none;
}

vector<TeamMemberAction> Player::getTeamMemberActions(const Creature* member) const {
  return {};
}

void Player::fillCurrentLevelInfo(GameInfo& gameInfo) const {
  auto level = getLevel();
  auto levels = getModel()->getDungeonBranch(level, getMemory());
  gameInfo.currentLevel = CurrentLevelInfo {
    level->name,
    levels.findElement(level).value_or(0),
    levels.transform([](auto level) { return level->name; }),
  };
}

void Player::refreshGameInfo(GameInfo& gameInfo) const {
  fillCurrentLevelInfo(gameInfo);
  gameInfo.messageBuffer = messageBuffer->current;
  gameInfo.isSingleMap = getGame()->isSingleModel();
  gameInfo.infoType = GameInfo::InfoType::PLAYER;
  SunlightInfo sunlightInfo = getGame()->getSunlightInfo();
  gameInfo.sunlightInfo.description = sunlightInfo.getText();
  gameInfo.sunlightInfo.timeRemaining = sunlightInfo.getTimeRemaining();
  gameInfo.time = creature->getGame()->getGlobalTime();
  auto contentFactory = getGame()->getContentFactory();
  gameInfo.scriptedHelp = contentFactory->scriptedHelp;
  gameInfo.playerInfo = PlayerInfo(creature, contentFactory);
  auto& info = *gameInfo.playerInfo.getReferenceMaybe<PlayerInfo>();
  if (getGame()->getPlayerCollective())
    info.controlMode = getGame()->getPlayerCreatures().size() == 1 ? PlayerInfo::LEADER : PlayerInfo::FULL;
  auto team = getTeam();
  if (team.size() > 1) {
    auto& timeQueue = getModel()->getTimeQueue();
    auto timeCmp = [&timeQueue, this](const Creature* c1, const Creature* c2) {
      auto sameModel1 = c1->getPosition().isSameModel(creature->getPosition());
      bool sameModel2 = c2->getPosition().isSameModel(creature->getPosition());
      auto values1 = make_tuple(sameModel1, c1->isPlayer());
      auto values2 = make_tuple(sameModel2, c2->isPlayer());
      if (values1 != values2)
        return values1 > values2;
      if (!sameModel1 && !sameModel2)
        return c1->getUniqueId() < c2->getUniqueId();
      return timeQueue.compareOrder(c1, c2);
    };
    sort(team.begin(), team.end(), timeCmp);
  }
  for (const Creature* c : team) {
    info.teamInfos.emplace_back(c, contentFactory);
    info.teamInfos.back().teamMemberActions = getTeamMemberActions(c);
  }
  info.lyingItems.clear();
  if (auto usageType = getUsableUsageType()) {
    auto furniture = creature->getPosition().getFurniture(FurnitureLayer::MIDDLE);
    auto question = FurnitureUsage::getUsageQuestion(*usageType, furniture->getName());
    ViewId questionViewId = ViewId("empty");
    if (auto& obj = furniture->getViewObject())
      questionViewId = obj->id();
    info.lyingItems.push_back(getFurnitureUsageInfo(question, questionViewId));
  }
  for (auto stack : creature->stackItems(creature->getPickUpOptions()))
    info.lyingItems.push_back(ItemInfo::get(creature, stack, contentFactory));
  info.commands = getCommands().transform([](const CommandInfo& info) -> PlayerInfo::CommandInfo { return info.commandInfo;});
  if (highlightedSpell) {
    auto availableSpells = creature->getSpellMap().getAvailable(creature);
    for (int i : All(availableSpells))
      info.spells[i].highlighted = (highlightedSpell == availableSpells[i]->getId());
  }
  if (tutorial)
    tutorial->refreshInfo(getGame(), gameInfo.tutorial);
}

ItemInfo Player::getFurnitureUsageInfo(const TString& question, ViewId viewId) const {
  return CONSTRUCT(ItemInfo,
    c.name = question;
    c.fullName = c.name;
//    c.description = {TSentence("CLICK_TO", c.name)};
    c.number = 1;
    c.viewId = {viewId};);
}

vector<Vec2> Player::getVisibleEnemies() const {
  return creature->getVisibleEnemies().transform(
      [](const Creature* c) { return c->getPosition().getCoord(); });
}

double Player::getAnimationTime() const {
  return getModel()->getMoveCounter();
}

Player::CenterType Player::getCenterType() const {
  return CenterType::FOLLOW;
}

const vector<Vec2>& Player::getUnknownLocations(const Level* level) const {
  return unknownLocations->getOnLevel(level);
}

optional<Vec2> Player::getPlayerPosition() const  {
  return creature->getPosition().getCoord();
}

vector<vector<Vec2>> Player::getPermanentPaths() const {
  vector<vector<Vec2>> ret;
  for (auto c : getTeam())
    if (auto task = c->getController()->getDragTask())
      if (auto target = task->getPosition())
        if (target->isSameLevel(creature->getPosition()) && target->isSameLevel(c->getPosition())) {
          auto res = LevelShortestPath(c, *target, 0).getPath()
              .filter([&, level = getLevel()](auto& pos) { return pos.getLevel() == level; } )
              .transform([&](auto& pos) { return pos.getCoord(); } );
          if (res.size() > 1)
            res.pop_back();
          ret.push_back(std::move(res));
        }
  return ret;
}

vector<Vec2> Player::getHighlightedPathTo(Vec2 v) const {
  Position target(v, getLevel());
  if (auto c = target.getCreature())
    if (getTeam().contains(c)) {
      auto res = c->getCurrentPath()
          .filter([&, level = getLevel()](auto& pos) { return pos.getLevel() == level; } )
          .transform([&](auto& pos) { return pos.getCoord(); } );
      if (res.size() > 1)
        res.pop_back();
      return res;
    }
  if (target.isSameLevel(creature->getPosition())) {
    LevelShortestPath path(creature, target, 0);
    return path.getPath().transform([](auto& pos) { return pos.getCoord(); });
  } else
    return {};
}

vector<vector<Vec2>> Player::getPathTo(UniqueEntity<Creature>::Id id, Vec2 to) const {
  Position target(to, getLevel());
  for (auto c : getTeam())
    if (c->getUniqueId() == id)
      if (target.isSameLevel(c->getPosition())) {
        LevelShortestPath path(c, target, 0);
        return {path.getPath().transform([](auto& pos) { return pos.getCoord(); })};
      }
  return {};
}

void Player::considerKeeperModeTravelMusic() {
  if (auto t = getModel()->getDefaultMusic()) {
    getGame()->setCurrentMusic(*t);
    return;
  }
  for (Collective* col : getModel()->getCollectives())
    if (col->getVillainType() == VillainType::MAIN && !col->isConquered() &&
        col->getTerritory().contains(creature->getPosition())) {
      getGame()->setCurrentMusic(MusicType::BATTLE);
      return;
    }
  getGame()->setCurrentMusic(MusicType::PEACEFUL);
}

void Player::scrollStairs(int diff) {
  auto model = creature->getPosition().getModel();
  auto curLevel = creature->getPosition().getLevel();
  for (auto targetLevel : model->getDungeonBranch(curLevel, getMemory()))
    if (targetLevel->depth == curLevel->depth + diff) {
      auto curPos = creature->getPosition();
      auto movement = creature->getMovementType();
      optional<Position> bestStairs;
      int bestDist = 10000000;
      for (auto key : targetLevel->getAllStairKeys()) {
        auto stairPos = targetLevel->getLandingSquares(key)[0];
        if (auto res = stairPos.getStairsTo(curPos, movement))
          if (res->second < bestDist) {
            bestStairs = stairPos;
            bestDist = res->second;
          }
      }
      if (bestStairs)
        target = *bestStairs;
    }
}

REGISTER_TYPE(ListenerTemplate<Player>)
