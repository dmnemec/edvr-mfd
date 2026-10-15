package main

import (
	"math"
	"strings"
)

// Body value constants based on Elite Dangerous community reverse-engineered formulas (MattG / EDAstro).
const (
	// massScalingQ is the mass-scaling coefficient for planetary exploration calculations.
	massScalingQ = 0.56591828

	// Star base values (k).
	starBaseDefault     = 1200.0
	starBaseWhiteDwarf  = 14057.0
	starBaseNeutronStar = 22628.0
	starBaseBlackHole   = 22628.0

	// Planet base values (k).
	planetBaseMetalRich   = 21790.0
	planetBaseELW         = 64831.0
	planetBaseWaterWorld  = 26995.0
	planetBaseAmmonia     = 96995.0
	planetBaseHMC         = 9654.0
	planetBaseRocky       = 300.0
	planetBaseIcy         = 500.0
	planetBaseRockyIce    = 500.0
	planetBaseGasGiant2   = 9654.0
	planetBaseGasGiantDef = 1656.0

	// Terraforming bonus base (k).
	terraformBaseBonus = 65631.0

	// Multipliers.
	firstDiscoveryMult = 2.6
	mappingMult        = 3.3333333333
	firstMappedMult    = 8.0956
	firstDiscFirstMap  = 36.99
)

// Planet classes and star types normalization.
func cleanType(t string) string {
	t = strings.TrimSpace(strings.ToLower(t))
	t = strings.TrimPrefix(t, "$")
	t = strings.TrimSuffix(t, ";")
	return t
}

// CalculateBodyValue estimates the exploration credit payout for a body (scan + map, incl. bonuses).
func CalculateBodyValue(planetClass, starType, terraformState string, mass float64, wasDiscovered, wasMapped, mappedByPilot bool) int64 {
	pClass := cleanType(planetClass)
	sClass := cleanType(starType)

	// Stars
	if sClass != "" || planetClass == "" {
		k := starBaseDefault
		if strings.HasPrefix(sClass, "d") || strings.Contains(sClass, "white dwarf") {
			k = starBaseWhiteDwarf
		} else if sClass == "n" || strings.Contains(sClass, "neutron") {
			k = starBaseNeutronStar
		} else if sClass == "h" || strings.Contains(sClass, "black hole") {
			k = starBaseBlackHole
		}

		val := k + (mass * k / 66.25)
		if val < starBaseDefault {
			val = starBaseDefault
		}
		if !wasDiscovered {
			val *= firstDiscoveryMult
		}
		return int64(math.Round(val))
	}

	// Planets
	var k float64
	isELW := false
	isTerraformable := strings.Contains(strings.ToLower(terraformState), "terraform")

	switch {
	case strings.Contains(pClass, "earth") || strings.Contains(pClass, "elw"):
		k = planetBaseELW
		isELW = true
	case strings.Contains(pClass, "water world") || strings.Contains(pClass, "water"):
		if isTerraformable {
			k = planetBaseELW
		} else {
			k = planetBaseWaterWorld
		}
	case strings.Contains(pClass, "ammonia"):
		k = planetBaseAmmonia
	case strings.Contains(pClass, "metal rich") || strings.Contains(pClass, "metal-rich"):
		k = planetBaseMetalRich
	case strings.Contains(pClass, "high metal"):
		k = planetBaseHMC
	case strings.Contains(pClass, "rocky ice"):
		k = planetBaseRockyIce
	case strings.Contains(pClass, "rocky"):
		k = planetBaseRocky
	case strings.Contains(pClass, "icy"):
		k = planetBaseIcy
	case strings.Contains(pClass, "class ii gas") || strings.Contains(pClass, "class 2"):
		k = planetBaseGasGiant2
	case strings.Contains(pClass, "gas giant") || strings.Contains(pClass, "giant"):
		k = planetBaseGasGiantDef
	default:
		k = planetBaseRocky
	}

	massPower := math.Pow(math.Max(mass, 0.00001), 0.2)
	baseVal := k + (k * massScalingQ * massPower)

	if isTerraformable && !isELW {
		baseVal += terraformBaseBonus + (terraformBaseBonus * massScalingQ * massPower)
	}

	// Payout multiplier based on mapping and first-discovery flags
	var val float64
	if mappedByPilot || wasMapped {
		if !wasDiscovered && !wasMapped {
			val = baseVal * firstDiscFirstMap
		} else if !wasMapped {
			val = baseVal * firstMappedMult
		} else {
			val = baseVal * mappingMult
		}
	} else {
		// Scan only (or unmapped)
		if !wasDiscovered {
			val = baseVal * firstDiscoveryMult
		} else {
			val = baseVal
		}
	}

	if val < 500 {
		val = 500
	}
	return int64(math.Round(val))
}

// Genus colony ranges in meters per §12.2 and Odyssey exobiology rules.
var GenusColonyRanges = map[string]float64{
	"aleoida":            150.0,
	"amphora plant":      150.0,
	"anemone":            100.0,
	"bacterium":          500.0,
	"bark mounds":        100.0,
	"brain tree":         100.0,
	"brain trees":        100.0,
	"cactoida":           300.0,
	"clypeus":            150.0,
	"concha":             150.0,
	"crystalline shards": 100.0,
	"electricae":         1000.0,
	"fonticulua":         500.0,
	"frutexa":            150.0,
	"fumerola":           100.0,
	"fungoida":           300.0,
	"osseus":             800.0,
	"recepta":            150.0,
	"sinuous tubers":     100.0,
	"stratum":            500.0,
	"tubus":              800.0,
	"tussock":            200.0,
}

// GetGenusColonyRange returns the required distance in meters between genetic samples for a genus.
func GetGenusColonyRange(genus string) float64 {
	g := cleanType(genus)
	for k, v := range GenusColonyRanges {
		if strings.Contains(g, k) || strings.Contains(k, g) {
			return v
		}
	}
	return 300.0 // Reasonable default fallback
}

// SpeciesBaseValues holds base credit payout values for exobiological species (Vista Genomics).
var SpeciesBaseValues = map[string]int64{
	"aleoida arcus":          7252500,
	"aleoida coronamus":      6284600,
	"aleoida gravis":         12934900,
	"aleoida laminiae":       3385200,
	"aleoida spica":          3385200,
	"amphora plant":          1628800,
	"anemone":                1499900,
	"bacterium acies":        1000000,
	"bacterium alcyoneum":    1658500,
	"bacterium aurasus":      1000000,
	"bacterium bullaris":     1152500,
	"bacterium cerbrus":      1689800,
	"bacterium informem":     8418000,
	"bacterium nebulus":      5289900,
	"bacterium omentum":      4638900,
	"bacterium scopulum":     4934500,
	"bacterium tela":         1949000,
	"bacterium verrata":      3897000,
	"bacterium vesicula":     1000000,
	"bacterium volu":         7774700,
	"bark mounds":            1471900,
	"brain tree":             1593700,
	"cactoida cortexum":      3667600,
	"cactoida lapis":         2483600,
	"cactoida peperatis":     2483600,
	"cactoida pullulanta":    3667600,
	"cactoida vermis":        16202800,
	"clypeus lacrimam":       8418000,
	"clypeus margaritus":     11873200,
	"clypeus speculumi":      16202800,
	"concha aureolas":        7774700,
	"concha biconcavis":      19010800,
	"concha labiata":         2352400,
	"concha renibus":         4572400,
	"crystalline shards":     1628800,
	"electricae pluma":       6284600,
	"electricae radialem":    6284600,
	"fonticulua campestris":  1000000,
	"fonticulua digitos":     1804100,
	"fonticulua fluctus":     20000000,
	"fonticulua lapida":      3111000,
	"fonticulua segmentatus": 19010800,
	"fonticulua upupam":      5727600,
	"frutexa acus":           7774700,
	"frutexa collum":         1639800,
	"frutexa fera":           1632500,
	"frutexa flabellum":      1808900,
	"frutexa flammasis":      10326000,
	"frutexa metallicum":     1632500,
	"frutexa sponsae":        5988000,
	"fumerola aquatis":       6284600,
	"fumerola carbosis":      6284600,
	"fumerola extremus":      16202800,
	"fumerola nitris":        7500900,
	"fungoida bullarum":      3703200,
	"fungoida gelata":        3330300,
	"fungoida setisis":       1670100,
	"fungoida stabitis":      2680300,
	"osseus cornibus":        1483000,
	"osseus discus":          12934900,
	"osseus fractus":         4027800,
	"osseus pellebantus":     9739000,
	"osseus pumice":          3156300,
	"osseus spiralis":        2404700,
	"recepta conditivus":     14313700,
	"recepta deltahedronix":  16202800,
	"recepta umbrux":         12934900,
	"sinuous tubers":         1514500,
	"stratum araneamus":      2448900,
	"stratum cucumisis":      16202800,
	"stratum excutitus":      2448900,
	"stratum frigus":         2637500,
	"stratum laminamus":      2788300,
	"stratum limaxus":        1362000,
	"stratum paleas":         1362000,
	"stratum tectonicas":     19010800,
	"tubus cavas":            11873200,
	"tubus compagibus":       7774700,
	"tubus conifer":          2415500,
	"tubus rosarium":         2637500,
	"tubus sororibus":        5727600,
	"tussock albata":         3252500,
	"tussock capillum":       7025800,
	"tussock caputus":        3472400,
	"tussock catena":         1766600,
	"tussock cultro":         1766600,
	"tussock divisa":         1766600,
	"tussock ignis":          1849000,
	"tussock pennata":        5853800,
	"tussock pennatis":       1000000,
	"tussock propagito":      1000000,
	"tussock serrati":        4447100,
	"tussock stigmasis":      19010800,
	"tussock triticum":       7774700,
	"tussock ventusa":        3227700,
	"tussock virgam":         14313700,
}

// GetSpeciesBaseValue returns the base payout for an exobiology species.
func GetSpeciesBaseValue(species string) int64 {
	s := cleanType(species)
	if val, ok := SpeciesBaseValues[s]; ok {
		return val
	}
	// Try prefix match or substring
	for k, v := range SpeciesBaseValues {
		if strings.Contains(s, k) || strings.Contains(k, s) {
			return v
		}
	}
	return 1000000 // Standard base fallback (1M CR)
}

// HaversineDistance calculates great-circle distance in meters between two lat/lon coordinates in degrees
// on a celestial body of given radius in meters.
func HaversineDistance(lat1Deg, lon1Deg, lat2Deg, lon2Deg, radiusMeters float64) float64 {
	if radiusMeters <= 0 {
		radiusMeters = 6371000.0 // Default Earth radius fallback
	}

	deg2rad := math.Pi / 180.0
	phi1 := lat1Deg * deg2rad
	phi2 := lat2Deg * deg2rad
	dPhi := (lat2Deg - lat1Deg) * deg2rad
	dLambda := (lon2Deg - lon1Deg) * deg2rad

	a := math.Sin(dPhi/2.0)*math.Sin(dPhi/2.0) +
		math.Cos(phi1)*math.Cos(phi2)*math.Sin(dLambda/2.0)*math.Sin(dLambda/2.0)
	c := 2.0 * math.Atan2(math.Sqrt(a), math.Sqrt(1.0-a))

	return radiusMeters * c
}
