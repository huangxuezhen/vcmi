#include "StdInc.h"
#include "CMapGenerator.h"

#include "../mapping/CMap.h"
#include "../VCMI_Lib.h"
#include "../CGeneralTextHandler.h"
#include "../mapping/CMapEditManager.h"
#include "../CObjectHandler.h"
#include "../CDefObjInfoHandler.h"
#include "../CTownHandler.h"
#include "../StringConstants.h"
#include "../filesystem/Filesystem.h"

CMapGenOptions::CMapGenOptions() : width(CMapHeader::MAP_SIZE_MIDDLE), height(CMapHeader::MAP_SIZE_MIDDLE), hasTwoLevels(false),
	playerCount(RANDOM_SIZE), teamCount(RANDOM_SIZE), compOnlyPlayerCount(0), compOnlyTeamCount(RANDOM_SIZE),
	waterContent(EWaterContent::RANDOM), monsterStrength(EMonsterStrength::RANDOM), mapTemplate(nullptr)
{
	resetPlayersMap();
}

si32 CMapGenOptions::getWidth() const
{
	return width;
}

void CMapGenOptions::setWidth(si32 value)
{
	assert(value >= 1);
	width = value;
}

si32 CMapGenOptions::getHeight() const
{
	return height;
}

void CMapGenOptions::setHeight(si32 value)
{
	assert(value >= 1);
	height = value;
}

bool CMapGenOptions::getHasTwoLevels() const
{
	return hasTwoLevels;
}

void CMapGenOptions::setHasTwoLevels(bool value)
{
	hasTwoLevels = value;
}

si8 CMapGenOptions::getPlayerCount() const
{
	return playerCount;
}

void CMapGenOptions::setPlayerCount(si8 value)
{
	assert((value >= 1 && value <= PlayerColor::PLAYER_LIMIT_I) || value == RANDOM_SIZE);
	playerCount = value;
	resetPlayersMap();
}

si8 CMapGenOptions::getTeamCount() const
{
	return teamCount;
}

void CMapGenOptions::setTeamCount(si8 value)
{
	assert(playerCount == RANDOM_SIZE || (value >= 0 && value < playerCount) || value == RANDOM_SIZE);
	teamCount = value;
}

si8 CMapGenOptions::getCompOnlyPlayerCount() const
{
	return compOnlyPlayerCount;
}

void CMapGenOptions::setCompOnlyPlayerCount(si8 value)
{
	assert(value == RANDOM_SIZE || (value >= 0 && value <= PlayerColor::PLAYER_LIMIT_I - playerCount));
	compOnlyPlayerCount = value;
	resetPlayersMap();
}

si8 CMapGenOptions::getCompOnlyTeamCount() const
{
	return compOnlyTeamCount;
}

void CMapGenOptions::setCompOnlyTeamCount(si8 value)
{
	assert(value == RANDOM_SIZE || compOnlyPlayerCount == RANDOM_SIZE || (value >= 0 && value <= std::max(compOnlyPlayerCount - 1, 0)));
	compOnlyTeamCount = value;
}

EWaterContent::EWaterContent CMapGenOptions::getWaterContent() const
{
	return waterContent;
}

void CMapGenOptions::setWaterContent(EWaterContent::EWaterContent value)
{
	waterContent = value;
}

EMonsterStrength::EMonsterStrength CMapGenOptions::getMonsterStrength() const
{
	return monsterStrength;
}

void CMapGenOptions::setMonsterStrength(EMonsterStrength::EMonsterStrength value)
{
	monsterStrength = value;
}

void CMapGenOptions::resetPlayersMap()
{
	players.clear();
	int realPlayersCnt = playerCount == RANDOM_SIZE ? static_cast<int>(PlayerColor::PLAYER_LIMIT_I) : playerCount;
	int realCompOnlyPlayersCnt = compOnlyPlayerCount == RANDOM_SIZE ? (PlayerColor::PLAYER_LIMIT_I - realPlayersCnt) : compOnlyPlayerCount;
	for(int color = 0; color < (realPlayersCnt + realCompOnlyPlayersCnt); ++color)
	{
		CPlayerSettings player;
		player.setColor(PlayerColor(color));
		player.setPlayerType((color >= realPlayersCnt) ? EPlayerType::COMP_ONLY : EPlayerType::AI);
		players[PlayerColor(color)] = player;
	}
}

const std::map<PlayerColor, CMapGenOptions::CPlayerSettings> & CMapGenOptions::getPlayersSettings() const
{
	return players;
}

void CMapGenOptions::setStartingTownForPlayer(PlayerColor color, si32 town)
{
	auto it = players.find(color);
	if(it == players.end()) assert(0);
	it->second.setStartingTown(town);
}

void CMapGenOptions::setPlayerTypeForStandardPlayer(PlayerColor color, EPlayerType::EPlayerType playerType)
{
	assert(playerType != EPlayerType::COMP_ONLY);
	auto it = players.find(color);
	if(it == players.end()) assert(0);
	it->second.setPlayerType(playerType);
}

const CRmgTemplate * CMapGenOptions::getMapTemplate() const
{
	return mapTemplate;
}

void CMapGenOptions::setMapTemplate(const CRmgTemplate * value)
{
    mapTemplate = value;
	//TODO validate & adapt options according to template
	assert(0);
}

const std::map<std::string, CRmgTemplate> & CMapGenOptions::getAvailableTemplates() const
{
	return CRmgTemplateStorage::get().getTemplates();
}

void CMapGenOptions::finalize()
{
	CRandomGenerator gen;
	finalize(gen);
}

void CMapGenOptions::finalize(CRandomGenerator & gen)
{
	if(!mapTemplate)
	{
		mapTemplate = getPossibleTemplate(gen);
		assert(mapTemplate);
	}

	if(playerCount == RANDOM_SIZE)
	{
		auto possiblePlayers = mapTemplate->getPlayers().getNumbers();
		possiblePlayers.erase(possiblePlayers.begin(), possiblePlayers.lower_bound(countHumanPlayers()));
		assert(!possiblePlayers.empty());
		playerCount = *std::next(possiblePlayers.begin(), gen.getInteger(0, possiblePlayers.size() - 1));
		updatePlayers();
	}
	if(teamCount == RANDOM_SIZE)
	{
		teamCount = gen.getInteger(0, playerCount - 1);
	}
	if(compOnlyPlayerCount == RANDOM_SIZE)
	{
		auto possiblePlayers = mapTemplate->getCpuPlayers().getNumbers();
		compOnlyPlayerCount = *std::next(possiblePlayers.begin(), gen.getInteger(0, possiblePlayers.size() - 1));
		updateCompOnlyPlayers();
	}
	if(compOnlyTeamCount == RANDOM_SIZE)
	{
		compOnlyTeamCount = gen.getInteger(0, std::max(compOnlyPlayerCount - 1, 0));
	}

	// 1 team isn't allowed
	if(teamCount == 1 && compOnlyPlayerCount == 0)
	{
		teamCount = 0;
	}

	if(waterContent == EWaterContent::RANDOM)
	{
		waterContent = static_cast<EWaterContent::EWaterContent>(gen.getInteger(0, 2));
	}
	if(monsterStrength == EMonsterStrength::RANDOM)
	{
		monsterStrength = static_cast<EMonsterStrength::EMonsterStrength>(gen.getInteger(0, 2));
	}
}

void CMapGenOptions::updatePlayers()
{
	// Remove AI players only from the end of the players map if necessary
	for(auto itrev = players.end(); itrev != players.begin();)
	{
		auto it = itrev;
		--it;
		if(players.size() == playerCount) break;
		if(it->second.getPlayerType() == EPlayerType::AI)
		{
			players.erase(it);
		}
		else
		{
			--itrev;
		}
	}
}

void CMapGenOptions::updateCompOnlyPlayers()
{
	auto totalPlayersCnt = playerCount + compOnlyPlayerCount;

	// Remove comp only players only from the end of the players map if necessary
	for(auto itrev = players.end(); itrev != players.begin();)
	{
		auto it = itrev;
		--it;
		if(players.size() <= totalPlayersCnt) break;
		if(it->second.getPlayerType() == EPlayerType::COMP_ONLY)
		{
			players.erase(it);
		}
		else
		{
			--itrev;
		}
	}

	// Add some comp only players if necessary
	auto compOnlyPlayersToAdd = totalPlayersCnt - players.size();
	for(int i = 0; i < compOnlyPlayersToAdd; ++i)
	{
		CPlayerSettings pSettings;
		pSettings.setPlayerType(EPlayerType::COMP_ONLY);
		pSettings.setColor(getNextPlayerColor());
		players[pSettings.getColor()] = pSettings;
	}
}

int CMapGenOptions::countHumanPlayers() const
{
	return static_cast<int>(boost::count_if(players, [](const std::pair<PlayerColor, CPlayerSettings> & pair)
	{
		return pair.second.getPlayerType() == EPlayerType::HUMAN;
	}));
}

PlayerColor CMapGenOptions::getNextPlayerColor() const
{
	for(PlayerColor i = PlayerColor(0); i < PlayerColor::PLAYER_LIMIT; i.advance(1))
	{
		if(!players.count(i))
		{
			return i;
		}
	}
	assert(0);
	return PlayerColor(0);
}

bool CMapGenOptions::checkOptions() const
{
	assert(countHumanPlayers() > 0);
	if(mapTemplate)
	{
		return true;
	}
	else
	{
		CRandomGenerator gen;
		return getPossibleTemplate(gen) != nullptr;
	}
}

const CRmgTemplate * CMapGenOptions::getPossibleTemplate(CRandomGenerator & gen) const
{
	// Find potential templates
	const auto & tpls = getAvailableTemplates();
	std::list<const CRmgTemplate *> potentialTpls;
	for(const auto & tplPair : tpls)
	{
		const auto & tpl = tplPair.second;
		CRmgTemplate::CSize tplSize(width, height, hasTwoLevels);
		if(tplSize >= tpl.getMinSize() && tplSize <= tpl.getMaxSize())
		{
			bool isPlayerCountValid = false;
			if(playerCount != RANDOM_SIZE)
			{
				if(tpl.getPlayers().isInRange(playerCount)) isPlayerCountValid = true;
			}
			else
			{
				// Human players shouldn't be banned when playing with random player count
				auto playerNumbers = tpl.getPlayers().getNumbers();
				if(playerNumbers.lower_bound(countHumanPlayers()) != playerNumbers.end())
				{
					isPlayerCountValid = true;
				}
			}

			if(isPlayerCountValid)
			{
				bool isCpuPlayerCountValid = false;
				if(compOnlyPlayerCount != RANDOM_SIZE)
				{
					if(tpl.getCpuPlayers().isInRange(compOnlyPlayerCount)) isCpuPlayerCountValid = true;
				}
				else
				{
					isCpuPlayerCountValid = true;
				}

				if(isCpuPlayerCountValid) potentialTpls.push_back(&tpl);
			}
		}
	}

	// Select tpl
	if(potentialTpls.empty())
	{
		return nullptr;
	}
	else
	{
		return *std::next(potentialTpls.begin(), gen.getInteger(0, potentialTpls.size() - 1));
	}
}

CMapGenOptions::CPlayerSettings::CPlayerSettings() : color(0), startingTown(RANDOM_TOWN), playerType(EPlayerType::AI)
{

}

PlayerColor CMapGenOptions::CPlayerSettings::getColor() const
{
	return color;
}

void CMapGenOptions::CPlayerSettings::setColor(PlayerColor value)
{
	assert(value >= PlayerColor(0) && value < PlayerColor::PLAYER_LIMIT);
	color = value;
}

si32 CMapGenOptions::CPlayerSettings::getStartingTown() const
{
	return startingTown;
}

void CMapGenOptions::CPlayerSettings::setStartingTown(si32 value)
{
	assert(value >= -1);
	if(value >= 0)
	{
		assert(value < static_cast<int>(VLC->townh->factions.size()));
		assert(VLC->townh->factions[value]->town != nullptr);
	}
	startingTown = value;
}

EPlayerType::EPlayerType CMapGenOptions::CPlayerSettings::getPlayerType() const
{
	return playerType;
}

void CMapGenOptions::CPlayerSettings::setPlayerType(EPlayerType::EPlayerType value)
{
	playerType = value;
}

CMapGenerator::CMapGenerator(const CMapGenOptions & mapGenOptions, int randomSeed /*= std::time(nullptr)*/) :
	mapGenOptions(mapGenOptions), randomSeed(randomSeed)
{
	gen.seed(randomSeed);
}

CMapGenerator::~CMapGenerator()
{

}

std::unique_ptr<CMap> CMapGenerator::generate()
{
	mapGenOptions.finalize(gen);

	map = make_unique<CMap>();
	editManager = map->getEditManager();
	editManager->getUndoManager().setUndoRedoLimit(0);
	addHeaderInfo();

	genZones();
	fillZones();

	return std::move(map);
}

std::string CMapGenerator::getMapDescription() const
{
	const std::string waterContentStr[3] = { "none", "normal", "islands" };
	const std::string monsterStrengthStr[3] = { "weak", "normal", "strong" };

    std::stringstream ss;
    ss << boost::str(boost::format(std::string("Map created by the Random Map Generator.\nTemplate was %s, Random seed was %d, size %dx%d") +
        ", levels %s, humans %d, computers %d, water %s, monster %s, second expansion map") % mapGenOptions.getMapTemplate()->getName() %
		randomSeed % map->width % map->height % (map->twoLevel ? "2" : "1") % static_cast<int>(mapGenOptions.getPlayerCount()) %
		static_cast<int>(mapGenOptions.getCompOnlyPlayerCount()) % waterContentStr[mapGenOptions.getWaterContent()] %
        monsterStrengthStr[mapGenOptions.getMonsterStrength()]);

	for(const auto & pair : mapGenOptions.getPlayersSettings())
	{
		const auto & pSettings = pair.second;
		if(pSettings.getPlayerType() == EPlayerType::HUMAN)
		{
			ss << ", " << GameConstants::PLAYER_COLOR_NAMES[pSettings.getColor().getNum()] << " is human";
		}
		if(pSettings.getStartingTown() != CMapGenOptions::CPlayerSettings::RANDOM_TOWN)
		{
			ss << ", " << GameConstants::PLAYER_COLOR_NAMES[pSettings.getColor().getNum()]
			   << " town choice is " << ETownType::names[pSettings.getStartingTown()];
		}
	}

	return ss.str();
}

void CMapGenerator::addPlayerInfo()
{
	// Calculate which team numbers exist
	std::array<std::list<int>, 2> teamNumbers; // 0= cpu/human, 1= cpu only
	int teamOffset = 0;
	for(int i = 0; i < 2; ++i)
	{
		int playerCount = i == 0 ? mapGenOptions.getPlayerCount() : mapGenOptions.getCompOnlyPlayerCount();
		int teamCount = i == 0 ? mapGenOptions.getTeamCount() : mapGenOptions.getCompOnlyTeamCount();

		if(playerCount == 0)
		{
			continue;
		}
		int playersPerTeam = playerCount /
				(teamCount == 0 ? playerCount : teamCount);
		int teamCountNorm = teamCount;
		if(teamCountNorm == 0)
		{
			teamCountNorm = playerCount;
		}
		for(int j = 0; j < teamCountNorm; ++j)
		{
			for(int k = 0; k < playersPerTeam; ++k)
			{
				teamNumbers[i].push_back(j + teamOffset);
			}
		}
		for(int j = 0; j < playerCount - teamCountNorm * playersPerTeam; ++j)
		{
			teamNumbers[i].push_back(j + teamOffset);
		}
		teamOffset += teamCountNorm;
	}

	// Team numbers are assigned randomly to every player
	for(const auto & pair : mapGenOptions.getPlayersSettings())
	{
		const auto & pSettings = pair.second;
		PlayerInfo player;
		player.canComputerPlay = true;
		int j = pSettings.getPlayerType() == EPlayerType::COMP_ONLY ? 1 : 0;
		if(j == 0)
		{
			player.canHumanPlay = true;
		}
		auto itTeam = std::next(teamNumbers[j].begin(), gen.getInteger(0, teamNumbers[j].size() - 1));
		player.team = TeamID(*itTeam);
		teamNumbers[j].erase(itTeam);
		map->players[pSettings.getColor().getNum()] = player;
	}

	map->howManyTeams = (mapGenOptions.getTeamCount() == 0 ? mapGenOptions.getPlayerCount() : mapGenOptions.getTeamCount())
			+ (mapGenOptions.getCompOnlyTeamCount() == 0 ? mapGenOptions.getCompOnlyPlayerCount() : mapGenOptions.getCompOnlyTeamCount());
}

void CMapGenerator::genZones()
{
	map->initTerrain();
	editManager->clearTerrain(&gen);
	editManager->getTerrainSelection().selectRange(MapRect(int3(0, 0, 0), mapGenOptions.getWidth(), mapGenOptions.getHeight()));
	editManager->drawTerrain(ETerrainType::GRASS, &gen);

	auto pcnt = mapGenOptions.getPlayerCount();
	auto w = mapGenOptions.getWidth();
	auto h = mapGenOptions.getHeight();


	auto tmpl = mapGenOptions.getMapTemplate();
	auto zones = tmpl->getZones();

	int player_per_side = zones.size() > 4 ? 3 : 2;
	int zones_cnt = zones.size() > 4 ? 9 : 4;
		
	logGlobal->infoStream() << boost::format("Map size %d %d, players per side %d") % w % h % player_per_side;

	int i = 0;
	int part_w = w/player_per_side;
	int part_h = h/player_per_side;
	for(auto const it : zones)
	{
		CRmgTemplateZone zone = it.second;
		std::vector<int3> shape;
		int left = part_w*(i%player_per_side);
		int top = part_h*(i/player_per_side);
		shape.push_back(int3(left, top,  0));
		shape.push_back(int3(left + part_w, top,  0));
		shape.push_back(int3(left + part_w, top + part_h,  0));
		shape.push_back(int3(left, top + part_h,  0));
		zone.setShape(shape);
		zone.setType(i < pcnt ? ETemplateZoneType::PLAYER_START : ETemplateZoneType::TREASURE);
		this->zones[it.first] = zone;
		++i;
	}
	logGlobal->infoStream() << "Zones generated successfully";
}

void CMapGenerator::fillZones()
{	
	logGlobal->infoStream() << "Started filling zones";
	for(auto it = zones.begin(); it != zones.end(); ++it)
	{
		it->second.fill(this);
	}	
	logGlobal->infoStream() << "Zones filled successfully";
}

void CMapGenerator::addHeaderInfo()
{
	map->version = EMapFormat::SOD;
	map->width = mapGenOptions.getWidth();
	map->height = mapGenOptions.getHeight();
	map->twoLevel = mapGenOptions.getHasTwoLevels();
	map->name = VLC->generaltexth->allTexts[740];
	map->description = getMapDescription();
	map->difficulty = 1;
	addPlayerInfo();
}

CRmgTemplateZone::CTownInfo::CTownInfo() : townCount(0), castleCount(0), townDensity(0), castleDensity(0)
{

}

int CRmgTemplateZone::CTownInfo::getTownCount() const
{
	return townCount;
}

void CRmgTemplateZone::CTownInfo::setTownCount(int value)
{
	if(value < 0) throw std::runtime_error("Negative value for town count not allowed.");
	townCount = value;
}

int CRmgTemplateZone::CTownInfo::getCastleCount() const
{
	return castleCount;
}

void CRmgTemplateZone::CTownInfo::setCastleCount(int value)
{
	if(value < 0) throw std::runtime_error("Negative value for castle count not allowed.");
	castleCount = value;
}

int CRmgTemplateZone::CTownInfo::getTownDensity() const
{
	return townDensity;
}

void CRmgTemplateZone::CTownInfo::setTownDensity(int value)
{
	if(value < 0) throw std::runtime_error("Negative value for town density not allowed.");
	townDensity = value;
}

int CRmgTemplateZone::CTownInfo::getCastleDensity() const
{
	return castleDensity;
}

void CRmgTemplateZone::CTownInfo::setCastleDensity(int value)
{
	if(value < 0) throw std::runtime_error("Negative value for castle density not allowed.");
	castleDensity = value;
}

CRmgTemplateZone::CTileInfo::CTileInfo():nearestObjectDistance(INT_MAX), obstacle(false), occupied(false), terrain(ETerrainType::WRONG) 
{

}

int CRmgTemplateZone::CTileInfo::getNearestObjectDistance() const
{
	return nearestObjectDistance;
}

void CRmgTemplateZone::CTileInfo::setNearestObjectDistance(int value)
{
	if(value < 0) throw std::runtime_error("Negative value for nearest object distance not allowed.");
	nearestObjectDistance = value;
}

bool CRmgTemplateZone::CTileInfo::isObstacle() const
{
	return obstacle;
}

void CRmgTemplateZone::CTileInfo::setObstacle(bool value)
{
	obstacle = value;
}

bool CRmgTemplateZone::CTileInfo::isOccupied() const
{
	return occupied;
}

void CRmgTemplateZone::CTileInfo::setOccupied(bool value)
{
	occupied = value;
}

ETerrainType CRmgTemplateZone::CTileInfo::getTerrainType() const
{
	return terrain;
}

void CRmgTemplateZone::CTileInfo::setTerrainType(ETerrainType value)
{
	terrain = value;
}

CRmgTemplateZone::CRmgTemplateZone() : id(0), type(ETemplateZoneType::PLAYER_START), size(1),
	townsAreSameType(false), matchTerrainToTown(true)
{
	townTypes = getDefaultTownTypes();
	terrainTypes = getDefaultTerrainTypes();
}

TRmgTemplateZoneId CRmgTemplateZone::getId() const
{
	return id;
}

void CRmgTemplateZone::setId(TRmgTemplateZoneId value)
{
	if(value <= 0) throw std::runtime_error("Zone id should be greater than 0.");
	id = value;
}

ETemplateZoneType::ETemplateZoneType CRmgTemplateZone::getType() const
{
	return type;
}
void CRmgTemplateZone::setType(ETemplateZoneType::ETemplateZoneType value)
{
	type = value;
}

int CRmgTemplateZone::getSize() const
{
	return size;
}

void CRmgTemplateZone::setSize(int value)
{
	if(value <= 0) throw std::runtime_error("Zone size needs to be greater than 0.");
	size = value;
}

boost::optional<int> CRmgTemplateZone::getOwner() const
{
	return owner;
}

void CRmgTemplateZone::setOwner(boost::optional<int> value)
{
	if(!(*value >= 0 && *value <= PlayerColor::PLAYER_LIMIT_I)) throw std::runtime_error("Owner has to be in range 0 to max player count.");
	owner = value;
}

const CRmgTemplateZone::CTownInfo & CRmgTemplateZone::getPlayerTowns() const
{
	return playerTowns;
}

void CRmgTemplateZone::setPlayerTowns(const CTownInfo & value)
{
	playerTowns = value;
}

const CRmgTemplateZone::CTownInfo & CRmgTemplateZone::getNeutralTowns() const
{
	return neutralTowns;
}

void CRmgTemplateZone::setNeutralTowns(const CTownInfo & value)
{
	neutralTowns = value;
}

bool CRmgTemplateZone::getTownsAreSameType() const
{
	return townsAreSameType;
}

void CRmgTemplateZone::setTownsAreSameType(bool value)
{
	townsAreSameType = value;
}

const std::set<TFaction> & CRmgTemplateZone::getTownTypes() const
{
	return townTypes;
}

void CRmgTemplateZone::setTownTypes(const std::set<TFaction> & value)
{
	townTypes = value;
}

std::set<TFaction> CRmgTemplateZone::getDefaultTownTypes() const
{
	std::set<TFaction> defaultTowns;
	auto towns = VLC->townh->getDefaultAllowed();
	for(int i = 0; i < towns.size(); ++i)
	{
		if(towns[i]) defaultTowns.insert(i);
	}
	return defaultTowns;
}

bool CRmgTemplateZone::getMatchTerrainToTown() const
{
	return matchTerrainToTown;
}

void CRmgTemplateZone::setMatchTerrainToTown(bool value)
{
	matchTerrainToTown = value;
}

const std::set<ETerrainType> & CRmgTemplateZone::getTerrainTypes() const
{
	return terrainTypes;
}

void CRmgTemplateZone::setTerrainTypes(const std::set<ETerrainType> & value)
{
	assert(value.find(ETerrainType::WRONG) == value.end() && value.find(ETerrainType::BORDER) == value.end() &&
		   value.find(ETerrainType::WATER) == value.end() && value.find(ETerrainType::ROCK) == value.end());
	terrainTypes = value;
}

std::set<ETerrainType> CRmgTemplateZone::getDefaultTerrainTypes() const
{
	std::set<ETerrainType> terTypes;
	static const ETerrainType::EETerrainType allowedTerTypes[] = { ETerrainType::DIRT, ETerrainType::SAND, ETerrainType::GRASS, ETerrainType::SNOW,
												   ETerrainType::SWAMP, ETerrainType::ROUGH, ETerrainType::SUBTERRANEAN, ETerrainType::LAVA };
	for(auto & allowedTerType : allowedTerTypes) terTypes.insert(allowedTerType);
	return terTypes;
}

boost::optional<TRmgTemplateZoneId> CRmgTemplateZone::getTerrainTypeLikeZone() const
{
	return terrainTypeLikeZone;
}

void CRmgTemplateZone::setTerrainTypeLikeZone(boost::optional<TRmgTemplateZoneId> value)
{
	terrainTypeLikeZone = value;
}

boost::optional<TRmgTemplateZoneId> CRmgTemplateZone::getTownTypeLikeZone() const
{
	return townTypeLikeZone;
}

void CRmgTemplateZone::setTownTypeLikeZone(boost::optional<TRmgTemplateZoneId> value)
{
	townTypeLikeZone = value;
}

bool CRmgTemplateZone::pointIsIn(int x, int y)
{
	int i, j;
	bool c = false;
	int nvert = shape.size();
	for (i = 0, j = nvert-1; i < nvert; j = i++) {
		if ( ((shape[i].y>y) != (shape[j].y>y)) &&
			(x < (shape[j].x-shape[i].x) * (y-shape[i].y) / (shape[j].y-shape[i].y) + shape[i].x) )
			c = !c;
	}
	return c;
}

void CRmgTemplateZone::setShape(std::vector<int3> shape)
{
	int z = -1;
	si32 minx = INT_MAX;
	si32 maxx = -1;
	si32 miny = INT_MAX;
	si32 maxy = -1;
	for(auto &point : shape)
	{
		if (z == -1)
			z = point.z;
		if (point.z != z)
			throw std::runtime_error("Zone shape points should lie on same z.");
		minx = std::min(minx, point.x);
		maxx = std::max(maxx, point.x);
		miny = std::min(miny, point.y);
		maxy = std::max(maxy, point.y);
	}
	this->shape = shape;
	for(int x = minx; x <= maxx; ++x)
	{
		for(int y = miny; y <= maxy; ++y)
		{
			if (pointIsIn(x, y))
			{
				tileinfo[int3(x,y,z)] = CTileInfo();
			}
		}
	}
}

int3 CRmgTemplateZone::getCenter()
{
	si32 cx = 0;
	si32 cy = 0;
	si32 area = 0;
	si32 sz = shape.size();
	//include last->first too
	for(si32 i = 0, j = sz-1; i < sz; j = i++) {
		si32 sf = (shape[i].x * shape[j].y - shape[j].x * shape[i].y);
		cx += (shape[i].x + shape[j].x) * sf;
		cy += (shape[i].y + shape[j].y) * sf;
		area += sf;
	}
	area /= 2;
	return int3(std::abs(cx/area/6), std::abs(cy/area/6), shape[0].z);
}

bool CRmgTemplateZone::fill(CMapGenerator* gen)
{
	std::vector<CGObjectInstance*> required_objects;
	if ((type == ETemplateZoneType::CPU_START) || (type == ETemplateZoneType::PLAYER_START))
	{
		logGlobal->infoStream() << "Preparing playing zone";
		int player_id = *owner - 1;
		auto & playerInfo = gen->map->players[player_id];
		if (playerInfo.canAnyonePlay())
		{
			PlayerColor player(player_id);
			auto  town = new CGTownInstance();
			town->ID = Obj::TOWN;
			int townId = gen->mapGenOptions.getPlayersSettings().find(player)->second.getStartingTown();
			
			static auto town_gen = gen->gen.getRangeI(0, 8);

			if(townId == CMapGenOptions::CPlayerSettings::RANDOM_TOWN) townId = town_gen(); // Default towns
			town->subID = townId;
			town->tempOwner = player;
			town->defInfo = VLC->dobjinfo->gobjs[town->ID][town->subID];
			town->builtBuildings.insert(BuildingID::FORT);
			town->builtBuildings.insert(BuildingID::DEFAULT);
			
			placeObject(gen, town, getCenter());
			logGlobal->infoStream() << "Placed object";

			logGlobal->infoStream() << "Fill player info " << player_id;
			auto & playerInfo = gen->map->players[player_id];
			// Update player info
			playerInfo.allowedFactions.clear();
			playerInfo.allowedFactions.insert(town->subID);
			playerInfo.hasMainTown = true;
			playerInfo.posOfMainTown = town->pos - int3(2, 0, 0);
			playerInfo.generateHeroAtMainTown = true;

			//required_objects.push_back(town);

			std::vector<Res::ERes> required_mines;
			required_mines.push_back(Res::ERes::WOOD);
			required_mines.push_back(Res::ERes::ORE);

			for(const auto res : required_mines)
			{			
				auto mine = new CGMine();
				mine->ID = Obj::MINE;
				mine->subID = static_cast<si32>(res);
				mine->producedResource = res;
				mine->producedQuantity = mine->defaultResProduction();
				mine->defInfo = VLC->dobjinfo->gobjs[mine->ID][mine->subID];
				required_objects.push_back(mine);
			}
		}
		else
		{			
			type = ETemplateZoneType::TREASURE;
			logGlobal->infoStream() << "Skipping this zone cause no player";
		}
	}
	logGlobal->infoStream() << "Creating required objects";
	for(const auto &obj : required_objects)
	{
		int3 pos;
		logGlobal->infoStream() << "Looking for place";
		if ( ! findPlaceForObject(gen, obj, 3, pos))		
		{
			logGlobal->errorStream() << "Failed to fill zone due to lack of space";
			//TODO CLEANUP!
			return false;
		}
		logGlobal->infoStream() << "Place found";

		placeObject(gen, obj, pos);
		logGlobal->infoStream() << "Placed object";
	}
	std::vector<CGObjectInstance*> guarded_objects;
	static auto res_gen = gen->gen.getRangeI(Res::ERes::WOOD, Res::ERes::GOLD);
	const double res_mindist = 5;
	do {
		auto obj = new CGResource();
		auto restype = static_cast<Res::ERes>(res_gen());
		obj->ID = Obj::RESOURCE;
		obj->subID = static_cast<si32>(restype);
		obj->amount = 0;
		obj->defInfo = VLC->dobjinfo->gobjs[obj->ID][obj->subID];
		
		int3 pos;
		if ( ! findPlaceForObject(gen, obj, res_mindist, pos))		
		{
			delete obj;
			break;
		}
		placeObject(gen, obj, pos);		
		if ((restype != Res::ERes::WOOD) && (restype != Res::ERes::ORE))
		{
			guarded_objects.push_back(obj);
		}
	} while(true);

	for(const auto &obj : guarded_objects)
	{
		if ( ! guardObject(gen, obj, 500))
		{
			//TODO, DEL obj from map
		}
	}

	auto sel = gen->editManager->getTerrainSelection();
	sel.clearSelection();
	for(auto it = tileinfo.begin(); it != tileinfo.end(); ++it)
	{
		if (it->second.isObstacle())
		{
			auto obj = new CGObjectInstance();
			obj->ID = static_cast<Obj>(130);
			obj->subID = 0;
			obj->defInfo = VLC->dobjinfo->gobjs[obj->ID][obj->subID];
			placeObject(gen, obj, it->first);
		}
	}
	logGlobal->infoStream() << boost::format("Filling %d with ROCK") % sel.getSelectedItems().size();
	//gen->editManager->drawTerrain(ETerrainType::ROCK, &gen->gen);
	logGlobal->infoStream() << "Zone filled successfully";
	return true;
}

bool CRmgTemplateZone::findPlaceForObject(CMapGenerator* gen, CGObjectInstance* obj, si32 min_dist, int3 &pos)
{
	//si32 min_dist = sqrt(tileinfo.size()/density);
	int best_distance = 0;
	bool result = false;
	si32 w = gen->map->width;
	si32 h = gen->map->height; 
	auto ow = obj->getWidth();
	auto oh = obj->getHeight();
	//logGlobal->infoStream() << boost::format("Min dist for density %f is %d") % density % min_dist;
	for(auto it = tileinfo.begin(); it != tileinfo.end(); ++it)
	{
		auto &ti = it->second;
		auto p = it->first;
		auto dist = ti.getNearestObjectDistance();
		//avoid borders
		if ((p.x < 3) || (w - p.x < 3) || (p.y < 3) || (h - p.y < 3))
			continue;
		if (!ti.isOccupied() && !ti.isObstacle()  && (dist >= min_dist) && (dist > best_distance))
		{
			best_distance = dist;
			pos = p;
			result = true;
		}
	}
	return result;
}

void CRmgTemplateZone::placeObject(CMapGenerator* gen, CGObjectInstance* object, const int3 &pos)
{
	logGlobal->infoStream() << boost::format("Insert object at %d %d") % pos.x % pos.y;
	object->pos = pos;
	gen->editManager->insertObject(object, pos);
	logGlobal->infoStream() << "Inserted object";
	auto points = object->getBlockedPos();
	if (object->isVisitable())
		points.emplace(pos + object->getVisitableOffset());
	points.emplace(pos);
	for(auto const &p : points)
	{		
		if (tileinfo.find(pos + p) != tileinfo.end())
		{
			tileinfo[pos + p].setOccupied(true);
		}
	}
	for(auto it = tileinfo.begin(); it != tileinfo.end(); ++it)
	{		
		si32 d = pos.dist2d(it->first);
		it->second.setNearestObjectDistance(std::min(d, it->second.getNearestObjectDistance()));
	}
}

bool CRmgTemplateZone::guardObject(CMapGenerator* gen, CGObjectInstance* object, si32 str)
{
	
	logGlobal->infoStream() << boost::format("Guard object at %d %d") % object->pos.x % object->pos.y;
	int3 visitable = object->pos + object->getVisitableOffset();
	std::vector<int3> tiles;
	for(int i = -1; i < 2; ++i)
	{
		for(int j = -1; j < 2; ++j)
		{
			auto it = tileinfo.find(visitable + int3(i, j, 0));
			if (it != tileinfo.end())
			{
				logGlobal->infoStream() << boost::format("Block at %d %d") % it->first.x % it->first.y;
				if ( ! it->second.isOccupied() &&  ! it->second.isObstacle())
				{
					tiles.push_back(it->first);
					it->second.setObstacle(true);
				}
			}
		}
	}
	if ( ! tiles.size())
	{		
		logGlobal->infoStream() << "Failed";
		return false;
	}
	auto guard_tile = *std::next(tiles.begin(), gen->gen.getInteger(0, tiles.size() - 1));
	tileinfo[guard_tile].setObstacle(false);
	auto guard = new CGCreature();
	guard->ID = Obj::RANDOM_MONSTER;
	guard->subID = 0;
	auto  hlp = new CStackInstance();
	hlp->count = 10;
	//type will be set during initialization
	guard->putStack(SlotID(0), hlp);

	guard->defInfo = VLC->dobjinfo->gobjs[guard->ID][guard->subID];
	guard->pos = guard_tile;
	gen->editManager->insertObject(guard, guard->pos);
	return true;
}

CRmgTemplateZoneConnection::CRmgTemplateZoneConnection() : zoneA(0), zoneB(0), guardStrength(0)
{

}

TRmgTemplateZoneId CRmgTemplateZoneConnection::getZoneA() const
{
	return zoneA;
}

void CRmgTemplateZoneConnection::setZoneA(TRmgTemplateZoneId value)
{
	zoneA = value;
}

TRmgTemplateZoneId CRmgTemplateZoneConnection::getZoneB() const
{
	return zoneB;
}

void CRmgTemplateZoneConnection::setZoneB(TRmgTemplateZoneId value)
{
	zoneB = value;
}

int CRmgTemplateZoneConnection::getGuardStrength() const
{
	return guardStrength;
}

void CRmgTemplateZoneConnection::setGuardStrength(int value)
{
	if(value < 0) throw std::runtime_error("Negative value for guard strenth not allowed.");
	guardStrength = value;
}

CRmgTemplate::CSize::CSize() : width(CMapHeader::MAP_SIZE_MIDDLE), height(CMapHeader::MAP_SIZE_MIDDLE), under(true)
{

}

CRmgTemplate::CSize::CSize(int width, int height, bool under) : under(under)
{
	setWidth(width);
	setHeight(height);
}

int CRmgTemplate::CSize::getWidth() const
{
	return width;
}

void CRmgTemplate::CSize::setWidth(int value)
{
	if(value <= 0) throw std::runtime_error("Width > 0 failed.");
	width = value;
}

int CRmgTemplate::CSize::getHeight() const
{
	return height;
}

void CRmgTemplate::CSize::setHeight(int value)
{
	if(value <= 0) throw std::runtime_error("Height > 0 failed.");
	height = value;
}

bool CRmgTemplate::CSize::getUnder() const
{
	return under;
}

void CRmgTemplate::CSize::setUnder(bool value)
{
	under = value;
}

bool CRmgTemplate::CSize::operator<=(const CSize & value) const
{
	if(width < value.width && height < value.height)
	{
		return true;
	}
	else if(width == value.width && height == value.height)
	{
		return under ? value.under : true;
	}
	else
	{
		return false;
	}
}

bool CRmgTemplate::CSize::operator>=(const CSize & value) const
{
	if(width > value.width && height > value.height)
	{
		return true;
	}
	else if(width == value.width && height == value.height)
	{
		return under ? true : !value.under;
	}
	else
	{
		return false;
	}
}

CRmgTemplate::CRmgTemplate()
{

}

const std::string & CRmgTemplate::getName() const
{
	return name;
}

void CRmgTemplate::setName(const std::string & value)
{
	name = value;
}

const CRmgTemplate::CSize & CRmgTemplate::getMinSize() const
{
	return minSize;
}

void CRmgTemplate::setMinSize(const CSize & value)
{
	minSize = value;
}

const CRmgTemplate::CSize & CRmgTemplate::getMaxSize() const
{
	return maxSize;
}

void CRmgTemplate::setMaxSize(const CSize & value)
{
	maxSize = value;
}

const CRmgTemplate::CPlayerCountRange & CRmgTemplate::getPlayers() const
{
	return players;
}

void CRmgTemplate::setPlayers(const CPlayerCountRange & value)
{
	players = value;
}

const CRmgTemplate::CPlayerCountRange & CRmgTemplate::getCpuPlayers() const
{
	return cpuPlayers;
}

void CRmgTemplate::setCpuPlayers(const CPlayerCountRange & value)
{
	cpuPlayers = value;
}

const std::map<TRmgTemplateZoneId, CRmgTemplateZone> & CRmgTemplate::getZones() const
{
	return zones;
}

void CRmgTemplate::setZones(const std::map<TRmgTemplateZoneId, CRmgTemplateZone> & value)
{
	zones = value;
}

const std::list<CRmgTemplateZoneConnection> & CRmgTemplate::getConnections() const
{
	return connections;
}

void CRmgTemplate::setConnections(const std::list<CRmgTemplateZoneConnection> & value)
{
	connections = value;
}

void CRmgTemplate::validate() const
{
	//TODO add some validation checks, throw on failure
}

void CRmgTemplate::CPlayerCountRange::addRange(int lower, int upper)
{
	range.push_back(std::make_pair(lower, upper));
}

void CRmgTemplate::CPlayerCountRange::addNumber(int value)
{
	range.push_back(std::make_pair(value, value));
}

bool CRmgTemplate::CPlayerCountRange::isInRange(int count) const
{
	for(const auto & pair : range)
	{
		if(count >= pair.first && count <= pair.second) return true;
	}
	return false;
}

std::set<int> CRmgTemplate::CPlayerCountRange::getNumbers() const
{
	std::set<int> numbers;
	for(const auto & pair : range)
	{
		for(int i = pair.first; i <= pair.second; ++i) numbers.insert(i);
	}
	return numbers;
}

const std::map<std::string, CRmgTemplate> & CRmgTemplateLoader::getTemplates() const
{
	return templates;
}

void CJsonRmgTemplateLoader::loadTemplates()
{
	const JsonNode rootNode(ResourceID("config/rmg.json"));
	for(const auto & templatePair : rootNode.Struct())
	{
		CRmgTemplate tpl;
		try
		{
			tpl.setName(templatePair.first);
			const auto & templateNode = templatePair.second;

			// Parse main template data
			tpl.setMinSize(parseMapTemplateSize(templateNode["minSize"].String()));
			tpl.setMaxSize(parseMapTemplateSize(templateNode["maxSize"].String()));
			tpl.setPlayers(parsePlayers(templateNode["players"].String()));
			tpl.setCpuPlayers(parsePlayers(templateNode["cpu"].String()));

			// Parse zones
			std::map<TRmgTemplateZoneId, CRmgTemplateZone> zones;
			for(const auto & zonePair : templateNode["zones"].Struct())
			{
				CRmgTemplateZone zone;
				auto zoneId = boost::lexical_cast<TRmgTemplateZoneId>(zonePair.first);
				zone.setId(zoneId);
				const auto & zoneNode = zonePair.second;
				zone.setType(parseZoneType(zoneNode["type"].String()));
				zone.setSize(zoneNode["size"].Float());
				if(!zoneNode["owner"].isNull()) zone.setOwner(zoneNode["owner"].Float());
				zone.setPlayerTowns(parseTemplateZoneTowns(zoneNode["playerTowns"]));
				zone.setNeutralTowns(parseTemplateZoneTowns(zoneNode["neutralTowns"]));
				zone.setTownTypes(parseTownTypes(zoneNode["townTypes"].Vector(), zone.getDefaultTownTypes()));
				zone.setMatchTerrainToTown(zoneNode["matchTerrainToTown"].Bool());
				zone.setTerrainTypes(parseTerrainTypes(zoneNode["terrainTypes"].Vector(), zone.getDefaultTerrainTypes()));
				zone.setTownsAreSameType((zoneNode["townsAreSameType"].Bool()));
				if(!zoneNode["terrainTypeLikeZone"].isNull()) zone.setTerrainTypeLikeZone(boost::lexical_cast<int>(zoneNode["terrainTypeLikeZone"].String()));
				if(!zoneNode["townTypeLikeZone"].isNull()) zone.setTownTypeLikeZone(boost::lexical_cast<int>(zoneNode["townTypeLikeZone"].String()));
				zones[zone.getId()] = zone;
			}
			tpl.setZones(zones);

			// Parse connections
			std::list<CRmgTemplateZoneConnection> connections;
			for(const auto & connPair : templateNode["connections"].Vector())
			{
				CRmgTemplateZoneConnection conn;
				conn.setZoneA(boost::lexical_cast<TRmgTemplateZoneId>(connPair["a"].String()));
				conn.setZoneB(boost::lexical_cast<TRmgTemplateZoneId>(connPair["b"].String()));
				conn.setGuardStrength(connPair["guard"].Float());
				connections.push_back(conn);
			}
			tpl.setConnections(connections);
			tpl.validate();
			templates[tpl.getName()] = tpl;
		}
		catch(const std::exception & e)
		{
			logGlobal->errorStream() << boost::format("Template %s has errors. Message: %s.") % tpl.getName() % std::string(e.what());
		}
	}
}

CRmgTemplate::CSize CJsonRmgTemplateLoader::parseMapTemplateSize(const std::string & text) const
{
	CRmgTemplate::CSize size;
	if(text.empty()) return size;

	std::vector<std::string> parts;
	boost::split(parts, text, boost::is_any_of("+"));
	static const std::map<std::string, int> mapSizeMapping = boost::assign::map_list_of("s", CMapHeader::MAP_SIZE_SMALL)
			("m", CMapHeader::MAP_SIZE_MIDDLE)("l", CMapHeader::MAP_SIZE_LARGE)("xl", CMapHeader::MAP_SIZE_XLARGE);
	auto it = mapSizeMapping.find(parts[0]);
	if(it == mapSizeMapping.end())
	{
		// Map size is given as a number representation
		const auto & numericalRep = parts[0];
		parts.clear();
		boost::split(parts, numericalRep, boost::is_any_of("x"));
		assert(parts.size() == 3);
		size.setWidth(boost::lexical_cast<int>(parts[0]));
		size.setHeight(boost::lexical_cast<int>(parts[1]));
		size.setUnder(boost::lexical_cast<int>(parts[2]) == 1);
	}
	else
	{
		size.setWidth(it->second);
		size.setHeight(it->second);
		size.setUnder(parts.size() > 1 ? parts[1] == std::string("u") : false);
	}
	return size;
}

ETemplateZoneType::ETemplateZoneType CJsonRmgTemplateLoader::parseZoneType(const std::string & type) const
{
	static const std::map<std::string, ETemplateZoneType::ETemplateZoneType> zoneTypeMapping = boost::assign::map_list_of
			("playerStart", ETemplateZoneType::PLAYER_START)("cpuStart", ETemplateZoneType::CPU_START)
			("treasure", ETemplateZoneType::TREASURE)("junction", ETemplateZoneType::JUNCTION);
	auto it = zoneTypeMapping.find(type);
	if(it == zoneTypeMapping.end()) throw std::runtime_error("Zone type unknown.");
	return it->second;
}

CRmgTemplateZone::CTownInfo CJsonRmgTemplateLoader::parseTemplateZoneTowns(const JsonNode & node) const
{
	CRmgTemplateZone::CTownInfo towns;
	towns.setTownCount(node["towns"].Float());
	towns.setCastleCount(node["castles"].Float());
	towns.setTownDensity(node["townDensity"].Float());
	towns.setCastleDensity(node["castleDensity"].Float());
	return towns;
}

std::set<TFaction> CJsonRmgTemplateLoader::parseTownTypes(const JsonVector & townTypesVector, const std::set<TFaction> & defaultTownTypes) const
{
	std::set<TFaction> townTypes;
	for(const auto & townTypeNode : townTypesVector)
	{
		auto townTypeStr = townTypeNode.String();
		if(townTypeStr == "all") return defaultTownTypes;

		bool foundFaction = false;
		for(auto factionPtr : VLC->townh->factions)
		{
			if(factionPtr->town != nullptr && townTypeStr == factionPtr->name)
			{
				townTypes.insert(factionPtr->index);
				foundFaction = true;
			}
		}
		if(!foundFaction) throw std::runtime_error("Given faction is invalid.");
	}
	return townTypes;
}

std::set<ETerrainType> CJsonRmgTemplateLoader::parseTerrainTypes(const JsonVector & terTypeStrings, const std::set<ETerrainType> & defaultTerrainTypes) const
{
	std::set<ETerrainType> terTypes;
	for(const auto & node : terTypeStrings)
	{
		const auto & terTypeStr = node.String();
		if(terTypeStr == "all") return defaultTerrainTypes;
		auto pos = vstd::find_pos(GameConstants::TERRAIN_NAMES, terTypeStr);
		if (pos != -1)
		{
			terTypes.insert(ETerrainType(pos));
		}
		else
		{
			throw std::runtime_error("Terrain type is invalid.");
		}
	}
	return terTypes;
}

CRmgTemplate::CPlayerCountRange CJsonRmgTemplateLoader::parsePlayers(const std::string & players) const
{
	CRmgTemplate::CPlayerCountRange playerRange;
	if(players.empty())
	{
		playerRange.addNumber(0);
		return playerRange;
	}
	std::vector<std::string> commaParts;
	boost::split(commaParts, players, boost::is_any_of(","));
	for(const auto & commaPart : commaParts)
	{
		std::vector<std::string> rangeParts;
		boost::split(rangeParts, commaPart, boost::is_any_of("-"));
		if(rangeParts.size() == 2)
		{
			auto lower = boost::lexical_cast<int>(rangeParts[0]);
			auto upper = boost::lexical_cast<int>(rangeParts[1]);
			playerRange.addRange(lower, upper);
		}
		else if(rangeParts.size() == 1)
		{
			auto val = boost::lexical_cast<int>(rangeParts.front());
			playerRange.addNumber(val);
		}
	}
	return playerRange;
}

boost::mutex CRmgTemplateStorage::smx;

CRmgTemplateStorage & CRmgTemplateStorage::get()
{
	TLockGuard _(smx);
	static CRmgTemplateStorage storage;
	return storage;
}

const std::map<std::string, CRmgTemplate> & CRmgTemplateStorage::getTemplates() const
{
	return templates;
}

CRmgTemplateStorage::CRmgTemplateStorage()
{
	auto jsonLoader = make_unique<CJsonRmgTemplateLoader>();
	jsonLoader->loadTemplates();
	const auto & tpls = jsonLoader->getTemplates();
	templates.insert(tpls.begin(), tpls.end());
}

CRmgTemplateStorage::~CRmgTemplateStorage()
{

}
