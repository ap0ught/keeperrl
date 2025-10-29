#include "tileset.h"
#include "directory_path.h"
#include "view_id.h"
#include "tile.h"
#include "view_object.h"
#include "game_config.h"
#include "tile_info.h"
#include "scripted_ui.h"
#include "clock.h"

void TileSet::addTile(string id, Tile tile) {
  tiles.insert(make_pair(ViewId(id.data()).getInternalId(), std::move(tile)));
}

void TileSet::addSymbol(string id, Tile tile) {
  symbols.insert(make_pair(ViewId(id.data()).getInternalId(), std::move(tile)));
}

Color TileSet::getColor(ViewId id) const {
  auto symbol = getReferenceMaybe(symbols, id.getInternalId());
  CHECK(!!symbol) << "No symbol found for : " << id.data();
  return symbol->color;
}

Color TileSet::getColor(const ViewObject& object) const {
  if (object.hasModifier(ViewObject::Modifier::INVISIBLE))
    return Color::DARK_GRAY;
  if (object.hasModifier(ViewObject::Modifier::HIDDEN))
    return Color::LIGHT_GRAY;
  auto color = getColor(object.id());
  if (object.hasModifier(ViewObject::Modifier::PLANNED))
    return Color(color.r / 2, color.g / 2, color.b / 2);
  return color;
}

const vector<TileCoord>& TileSet::getTileCoord(const string& s) const {
  return tileCoords.at(s);
}

const vector<string> TileSet::getSpriteMods() const {
  return spriteMods;
}

const vector<TileCoord>& TileSet::byName(const string& s) {
  if (!tileCoords.count(s))
    return tileCoords.begin()->second;
  return tileCoords.at(s);
}

Tile TileSet::sprite(const string& s) {
  return Tile::byCoord(byName(s));
}

Tile TileSet::empty() {
  return sprite("empty");
}

Tile TileSet::getRoadTile(const string& prefix) {
  return sprite(prefix)
    .addConnection({Dir::E, Dir::W}, byName(prefix + "ew"))
    .addConnection({Dir::W}, byName(prefix + "w"))
    .addConnection({Dir::E}, byName(prefix + "e"))
    .addConnection({Dir::S}, byName(prefix + "s"))
    .addConnection({Dir::N, Dir::S}, byName(prefix + "ns"))
    .addConnection({Dir::N}, byName(prefix + "n"))
    .addConnection({Dir::S, Dir::E}, byName(prefix + "se"))
    .addConnection({Dir::S, Dir::W}, byName(prefix + "sw"))
    .addConnection({Dir::N, Dir::E}, byName(prefix + "ne"))
    .addConnection({Dir::N, Dir::W}, byName(prefix + "nw"))
    .addConnection({Dir::N, Dir::E, Dir::S, Dir::W}, byName(prefix + "nesw"))
    .addConnection({Dir::E, Dir::S, Dir::W}, byName(prefix + "esw"))
    .addConnection({Dir::N, Dir::S, Dir::W}, byName(prefix + "nsw"))
    .addConnection({Dir::N, Dir::E, Dir::S}, byName(prefix + "nes"))
    .addConnection({Dir::N, Dir::E, Dir::W}, byName(prefix + "new"));
}

Tile TileSet::getWallTile(const string& prefix) {
  return sprite(prefix)
    .addConnection({Dir::E}, byName(prefix + "e"))
    .addConnection({Dir::E, Dir::W}, byName(prefix + "ew"))
    .addConnection({Dir::W}, byName(prefix + "w"))
    .addConnection({Dir::S}, byName(prefix + "s"))
    .addConnection({Dir::N, Dir::S}, byName(prefix + "ns"))
    .addConnection({Dir::N}, byName(prefix + "n"))
    .addConnection({Dir::E, Dir::S}, byName(prefix + "es"))
    .addConnection({Dir::S, Dir::W}, byName(prefix + "sw"))
    .addConnection({Dir::N, Dir::E}, byName(prefix + "ne"))
    .addConnection({Dir::N, Dir::W}, byName(prefix + "nw"))
    .addConnection({Dir::N, Dir::E, Dir::S, Dir::W}, byName(prefix + "nesw"))
    .addConnection({Dir::E, Dir::S, Dir::W}, byName(prefix + "esw"))
    .addConnection({Dir::N, Dir::S, Dir::W}, byName(prefix + "nsw"))
    .addConnection({Dir::N, Dir::E, Dir::S}, byName(prefix + "nes"))
    .addConnection({Dir::N, Dir::E, Dir::W}, byName(prefix + "new"))
    .setConnectionId(ViewId("wall"));
}

Tile TileSet::getMountainTile(const string& spriteName, const string& prefix) {
  return sprite(spriteName)
    .addCorner({Dir::S, Dir::W}, {Dir::W}, byName(prefix + "sw_w"))
    .addCorner({Dir::N, Dir::W}, {Dir::W}, byName(prefix + "nw_w"))
    .addCorner({Dir::S, Dir::E}, {Dir::E}, byName(prefix + "se_e"))
    .addCorner({Dir::N, Dir::E}, {Dir::E}, byName(prefix + "ne_e"))
    .addCorner({Dir::N, Dir::W}, {Dir::N}, byName(prefix + "nw_n"))
    .addCorner({Dir::N, Dir::E}, {Dir::N}, byName(prefix + "ne_n"))
    .addCorner({Dir::S, Dir::W}, {Dir::S}, byName(prefix + "sw_s"))
    .addCorner({Dir::S, Dir::E}, {Dir::S}, byName(prefix + "se_s"))
    .addCorner({Dir::N, Dir::W}, {}, byName(prefix + "nw"))
    .addCorner({Dir::N, Dir::E}, {}, byName(prefix + "ne"))
    .addCorner({Dir::S, Dir::W}, {}, byName(prefix + "sw"))
    .addCorner({Dir::S, Dir::E}, {}, byName(prefix + "se"))
    .addCorner({Dir::S, Dir::E, Dir::SE}, {Dir::S, Dir::E}, byName(prefix + "sese_se"))
    .addCorner({Dir::S, Dir::W, Dir::SW}, {Dir::S, Dir::W}, byName(prefix + "swsw_sw"))
    .addCorner({Dir::N, Dir::E, Dir::NE}, {Dir::N, Dir::E}, byName(prefix + "nene_ne"))
    .addCorner({Dir::N, Dir::W, Dir::NW}, {Dir::N, Dir::W}, byName(prefix + "nwnw_nw"))
    .setConnectionId(ViewId("mountain"));
}

Tile TileSet::getWaterTile(const string& background, const string& prefix) {
  return empty().addBackground(byName(background))
    .addConnection({Dir::N, Dir::E, Dir::S, Dir::W}, byName("empty"))
    .addConnection({Dir::E, Dir::S, Dir::W}, byName(prefix + "esw"))
    .addConnection({Dir::N, Dir::E, Dir::W}, byName(prefix + "new"))
    .addConnection({Dir::N, Dir::S, Dir::W}, byName(prefix + "nsw"))
    .addConnection({Dir::N, Dir::E, Dir::S}, byName(prefix + "nes"))
    .addConnection({Dir::N, Dir::E}, byName(prefix + "ne"))
    .addConnection({Dir::E, Dir::S}, byName(prefix + "es"))
    .addConnection({Dir::S, Dir::W}, byName(prefix + "sw"))
    .addConnection({Dir::N, Dir::W}, byName(prefix + "nw"))
    .addConnection({Dir::S}, byName(prefix + "s"))
    .addConnection({Dir::N}, byName(prefix + "n"))
    .addConnection({Dir::W}, byName(prefix + "w"))
    .addConnection({Dir::E}, byName(prefix + "e"))
    .addConnection({Dir::N, Dir::S}, byName(prefix + "ns"))
    .addConnection({Dir::E, Dir::W}, byName(prefix + "ew"))
    .addConnection({}, byName(prefix + "all"));
}

Tile TileSet::getExtraBorderTile(const string& prefix) {
  return sprite(prefix)
    .addExtraBorder({Dir::W, Dir::N}, byName(prefix + "wn"))
    .addExtraBorder({Dir::E, Dir::N}, byName(prefix + "en"))
    .addExtraBorder({Dir::E, Dir::S}, byName(prefix + "es"))
    .addExtraBorder({Dir::W, Dir::S}, byName(prefix + "ws"))
    .addExtraBorder({Dir::W, Dir::N, Dir::E}, byName(prefix + "wne"))
    .addExtraBorder({Dir::S, Dir::N, Dir::E}, byName(prefix + "sne"))
    .addExtraBorder({Dir::S, Dir::W, Dir::E}, byName(prefix + "swe"))
    .addExtraBorder({Dir::S, Dir::W, Dir::N}, byName(prefix + "swn"))
    .addExtraBorder({Dir::S, Dir::W, Dir::N, Dir::E}, byName(prefix + "swne"))
    .addExtraBorder({Dir::N}, byName(prefix + "n"))
    .addExtraBorder({Dir::E}, byName(prefix + "e"))
    .addExtraBorder({Dir::S}, byName(prefix + "s"))
    .addExtraBorder({Dir::W}, byName(prefix + "w"));
}

Tile TileSet::customConnections(const string& spriteName, const vector<TileInfo::Connection>& elems) {
  auto ret = sprite(spriteName);
  DirSet allDirs;
  for (auto& elem : elems) {
    ret.addConnection(elem.dirs, byName(elem.spriteName));
    for (auto d : elem.dirs)
      allDirs.insert(d);
  }
  return ret.setConnectionMask(allDirs);
}

void TileSet::loadModdedTiles(const vector<TileInfo>& tiles, bool useTiles) {
  for (auto& tile : tiles) {
    if (useTiles) {
      auto spriteName = tile.sprite.value_or(tile.viewId.data());
      //USER_CHECK(tileCoords.count(spriteName)) << "Sprite file not found: " << spriteName;
      auto t = [&] {
        if (!tile.connections.empty())
          return customConnections(spriteName, tile.connections);
        if (tile.wallConnections)
          return getWallTile(spriteName);
        if (tile.roadConnections)
          return getRoadTile(spriteName);
        if (tile.mountainSides)
          return getMountainTile(spriteName, *tile.mountainSides);
        if (tile.waterSides)
          return getWaterTile(spriteName, *tile.waterSides);
        if (!tile.extraBorders.empty()) {
          auto ret = getExtraBorderTile(spriteName);
          for (auto& elem : tile.extraBorders)
            ret.addExtraBorderId(ViewId(elem.data()));
          return ret;
        }
        return sprite(spriteName);
      }();
      if (tile.spriteColor)
        t.setColor(*tile.spriteColor);
      if (tile.roundShadow)
        t.setRoundShadow();
      if (tile.wallShadow)
        t.setWallShadow();
      if (tile.background)
        t.addBackground(byName(*tile.background));
      if (tile.moveUp)
        t.setMoveUp();
      if (tile.southSide)
        t.addOption(Dir::S, byName(*tile.southSide));
      if (tile.connectionId)
        t.setConnectionId(*tile.connectionId);
      if (tile.fx)
        t.setFX(*tile.fx);
      t.highlightAbove = tile.highlightAbove;
      t.animated = tile.animated;
      t.canMirror = tile.canMirror;
      t.weaponOrigin = tile.weaponOrigin;
      addTile(tile.viewId.data(), std::move(t));
    }
    addSymbol(tile.viewId.data(), symbol(tile.symbol, tile.color, tile.isSymbolFont));
  }
}

void TileSet::loadUnicode() {
  addSymbol("accept_immigrant", symbol(u8"✓", Color::GREEN, true));
  addSymbol("reject_immigrant", symbol(u8"✘", Color::RED, true));
  addSymbol("fog_of_war_corner", symbol(u8" ", Color::WHITE));
  addSymbol("tutorial_entrance", symbol(u8" ", Color::LIGHT_GREEN));
}

void TileSet::loadTiles() {
  addTile("accept_immigrant", symbol(u8"✓", Color::GREEN, true));
  addTile("reject_immigrant", symbol(u8"✘", Color::RED, true));
  addTile("fog_of_war_corner", sprite("fogofwarall")
      .addConnection({Dir::NE}, byName("fogofwarcornne"))
      .addConnection({Dir::NW}, byName("fogofwarcornnw"))
      .addConnection({Dir::SE}, byName("fogofwarcornse"))
      .addConnection({Dir::SW}, byName("fogofwarcornsw")));
#ifndef RELEASE
  addTile("tutorial_entrance", symbol(u8"?", Color::YELLOW));
#else
  addTile("tutorial_entrance", sprite("empty"));
#endif
}

Tile TileSet::symbol(const string& s, Color id, bool symbol) {
  return Tile::fromString(s, id, symbol);
}

TileSet::TileSet(const DirectoryPath& defaultDir, const DirectoryPath& modsDir, const DirectoryPath& scriptedHelpDir)
    : defaultDir(defaultDir), modsDir(modsDir), scriptedHelpDir(scriptedHelpDir) {
}

void TileSet::clear() {
  textures.clear();
}

void TileSet::setTilePaths(const TilePaths& p) {
  tilePaths = p;
  reload();
}

void TileSet::setTilePathsAndReload(const TilePaths& p) {
  clear();
  setTilePaths(p);
  loadTextures();
}

void TileSet::loadScriptedTextures(const DirectoryPath& dir, const FilePath& commonPath) {
  auto stripExtensions = [] (const FilePath& path) {
    string ret = path.getFileName();
    ret = ret.substr(0, ret.size() - 4);
    return ret;
  };
  for (auto file : dir.getFiles()) {
    if (file.getFileName() == "common.txt"_s)
      continue;
    if (file.hasSuffix(".png"))
      scriptedUITextures.insert(make_pair(stripExtensions(file), Texture(file)));
    else
    if (file.hasSuffix(".txt")) {
      ScriptedUI elem;
      while (1) {
        if (auto err = PrettyPrinting::parseObject(elem, {commonPath, file}, nullptr))
          USER_INFO << file.getFileName() << ": " << *err;
        else
          break;
      }
      scriptedUI.insert(make_pair(stripExtensions(file), std::move(elem)));
    }
  }
}

void TileSet::reload() {
  tiles.clear();
  symbols.clear();
  tileCoords.clear();
  spriteMods.clear();
  auto reloadDir = [&] (const DirectoryPath& path, bool overwrite) {
    bool hadTiles = false;
    hadTiles |= loadTilesFromDir(path.subdirectory("orig8"), Vec2(8, 8), overwrite);
    hadTiles |= loadTilesFromDir(path.subdirectory("orig16"), Vec2(16, 16), overwrite);
    hadTiles |= loadTilesFromDir(path.subdirectory("orig24"), Vec2(24, 24), overwrite);
    hadTiles |= loadTilesFromDir(path.subdirectory("orig30"), Vec2(30, 30), overwrite);
    return hadTiles;
  };
  reloadDir(defaultDir, true);
  for (auto& mod : tilePaths->mainMods)
    if (reloadDir(modsDir.subdirectory(mod), true))
      spriteMods.push_back(mod);
  for (auto& subdir : tilePaths->mergedMods)
    if (reloadDir(modsDir.subdirectory(subdir), false) && !spriteMods.contains(subdir))
      spriteMods.push_back(subdir);
  loadUnicode();
}

const Tile& TileSet::getTile(ViewId viewId, bool sprite) const {
  auto id = viewId.getInternalId();
  if (sprite && tiles.count(id))
    return tiles.at(id);
  else if (auto symbol = getReferenceMaybe(symbols, id))
    return *symbol;
  else {
    static Tile unknown = Tile::fromString("?", Color::GREEN);
    return unknown;
  }
}

constexpr int textureWidth = 720;

static int getNumFrames(const vector<FilePath>& files, int tileWidth) {
  int ret = 0;
  bool firstError = true;
  for (int i : All(files))
    if (SDL::SDL_Surface* im = SDL::IMG_Load(files[i].getPath())) {
      auto dest = OnExit([&] { SDL::SDL_FreeSurface(im); });
      ret += im->w / tileWidth;
    } else
    if (firstError) {
      USER_INFO << "Error loading image " << files[i].getPath() << ": " << SDL::IMG_GetError();
      firstError = true;
    }
  return ret;
}

static string getSantaSprite(const string& sprite) {
  vector<vector<const char*>> viewIds {{ "keeper1", "keeper2", "keeper3", "keeper4", "imp", "special_tree"},
        {"santa_keeper1", "santa_keeper2", "santa_keeper3", "santa_keeper4", "santa_imp", "xmas_tree" }};
  for (int column : Range(2))
    for (int i : All(viewIds[column]))
      if (sprite == viewIds[column][i])
        return viewIds[1 - column][i];
  return sprite;
}

bool TileSet::loadTilesFromDir(const DirectoryPath& path, Vec2 size, bool overwrite) {
  if (!path.exists())
    return false;
  const static string imageSuf = ".png";
  auto files = path.getFiles().filter([](const FilePath& f) { return f.hasSuffix(imageSuf);});
  if (files.empty())
    return false;
  int rowLength = textureWidth / size.x;
  const auto numFrames = getNumFrames(files, size.x);
  SDL::SDL_Surface* image = Texture::createSurface(textureWidth, (numFrames / rowLength + 1) * size.y);
  SDL::SDL_SetSurfaceBlendMode(image, SDL::SDL_BLENDMODE_NONE);
  CHECK(image) << SDL::SDL_GetError();
  int frameCount = 0;
  vector<pair<string, Vec2>> addedPositions;
  for (int i : All(files))
    if (SDL::SDL_Surface* im = SDL::IMG_Load(files[i].getPath())) {
      auto dest = OnExit([&] { SDL::SDL_FreeSurface(im); });
      SDL::SDL_SetSurfaceBlendMode(im, SDL::SDL_BLENDMODE_NONE);
      USER_CHECK(im) << files[i] << ": "<< SDL::IMG_GetError();
      USER_CHECK((im->w % size.x == 0) && im->h == size.y) << files[i] << " has wrong size " << im->w << " " << im->h;
      string fileName = files[i].getFileName();
      string spriteName = fileName.substr(0, fileName.size() - imageSuf.size());
      if (tileCoords.count(spriteName)) {
        if (overwrite)
          tileCoords.erase(spriteName);
        else
          continue;
      }
      for (int frame : Range(im->w / size.x)) {
        SDL::SDL_Rect dest;
        int posX = frameCount % rowLength;
        int posY = frameCount / rowLength;
        dest.x = size.x * posX;
        dest.y = size.y * posY;
        SDL::SDL_Rect src;
        src.x = frame * size.x;
        src.y = 0;
        src.w = size.x;
        src.h = size.y;
        SDL_BlitSurface(im, &src, image, &dest);
        addedPositions.emplace_back(spriteName, Vec2(posX, posY));
        INFO << "Loading tile sprite " << fileName << " at " << posX << "," << posY;
        ++frameCount;
      }
    }
  texturesTmp.push_back({image, addedPositions});
  bool isChristmas = Clock::isChristmas();
  for (auto& pos : addedPositions) {
    auto spriteName = isChristmas ? getSantaSprite(pos.first) : pos.first;
    tileCoords[spriteName].push_back({size, pos.second, nullptr});
  }
  return true;
}

void TileSet::loadTextures() {
  for (auto& elem : texturesTmp) {
    textures.push_back(make_unique<Texture>(elem.image));
    SDL::SDL_FreeSurface(elem.image);
    for (auto& pos : elem.addedPositions)
      for (auto& coord : tileCoords[pos.first])
        coord.texture = textures.back().get();
  }
  for (auto& elem : tileCoords)
    for (auto& coord : elem.second)
      CHECK(!!coord.texture);
  texturesTmp.clear();
  bool useTiles = !tileCoords.empty();
  if (useTiles)
    loadTiles();
  loadModdedTiles(tilePaths->definitions, useTiles);
  scriptedUITextures.clear();
  scriptedUI.clear();
  auto scriptCommonFile = scriptedHelpDir.file("common.txt");
  loadScriptedTextures(scriptedHelpDir, scriptCommonFile);
  for (auto& mod : tilePaths->mainMods) {
    loadScriptedTextures(modsDir.subdirectory(mod).subdirectory("help"), scriptCommonFile);
  }
}
