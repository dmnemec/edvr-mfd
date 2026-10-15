package elite

// Elite Dangerous Status.json Flags bit constants (Bit 0 to Bit 31).
const (
	FlagDocked               uint64 = 1 << 0  // Docked, on a landing pad
	FlagLanded               uint64 = 1 << 1  // Landed, on planet surface
	FlagLandingGearDown      uint64 = 1 << 2  // Landing Gear Down
	FlagShieldsUp            uint64 = 1 << 3  // Shields Up
	FlagSupercruise          uint64 = 1 << 4  // Supercruise
	FlagFlightAssistOff      uint64 = 1 << 5  // Flight Assist Off
	FlagHardpointsDeployed   uint64 = 1 << 6  // Hardpoints Deployed
	FlagInWing               uint64 = 1 << 7  // In Wing / Team
	FlagLightsOn             uint64 = 1 << 8  // Ship Lights On
	FlagCargoScoopDeployed   uint64 = 1 << 9  // Cargo Scoop Deployed
	FlagSilentRunning        uint64 = 1 << 10 // Silent Running
	FlagScoopingFuel         uint64 = 1 << 11 // Scooping Fuel
	FlagSrvHandbrake         uint64 = 1 << 12 // SRV Handbrake
	FlagSrvUsingTurret       uint64 = 1 << 13 // SRV using Turret view
	FlagSrvTurretRetracted   uint64 = 1 << 14 // SRV Turret retracted
	FlagSrvDriveAssist       uint64 = 1 << 15 // SRV Drive Assist
	FlagFsdMassLocked        uint64 = 1 << 16 // FSD MassLocked
	FlagFsdCharging          uint64 = 1 << 17 // FSD Charging
	FlagFsdCooldown          uint64 = 1 << 18 // FSD Cooldown
	FlagLowFuel              uint64 = 1 << 19 // Low Fuel (< 25%)
	FlagOverheating          uint64 = 1 << 20 // Over Heating (> 100%)
	FlagHasLatLong           uint64 = 1 << 21 // Has Lat/Long coordinates
	FlagIsInDanger           uint64 = 1 << 22 // Is In Danger
	FlagBeingInterdicted     uint64 = 1 << 23 // Being Interdicted
	FlagInMainShip           uint64 = 1 << 24 // In Main Ship
	FlagInFighter            uint64 = 1 << 25 // In Fighter
	FlagInSRV                uint64 = 1 << 26 // In SRV
	FlagHudInAnalysisMode    uint64 = 1 << 27 // HUD in Analysis mode
	FlagNightVision          uint64 = 1 << 28 // Night Vision
	FlagAltFromAvgRadius     uint64 = 1 << 29 // Altitude from Average radius
	FlagFsdJump              uint64 = 1 << 30 // FSD Jump (hyperspace)
	FlagSrvHighBeam          uint64 = 1 << 31 // SRV High Beam
)

// Elite Dangerous Status.json Flags2 bit constants (Odyssey on-foot / multicrew).
const (
	Flag2OnFoot                  uint64 = 1 << 0  // On Foot
	Flag2InTaxi                  uint64 = 1 << 1  // In Taxi
	Flag2InMulticrew             uint64 = 1 << 2  // In Multicrew (crew member)
	Flag2OnFootInStation         uint64 = 1 << 3  // On Foot in Station
	Flag2OnFootOnPlanet          uint64 = 1 << 4  // On Foot on Planet
	Flag2AimDownSight            uint64 = 1 << 5  // Aim Down Sight
	Flag2LowOxygen               uint64 = 1 << 6  // Low Oxygen
	Flag2LowHealth               uint64 = 1 << 7  // Low Health
	Flag2Cold                    uint64 = 1 << 8  // Cold
	Flag2Hot                     uint64 = 1 << 9  // Hot
	Flag2VeryCold                uint64 = 1 << 10 // Very Cold
	Flag2VeryHot                 uint64 = 1 << 11 // Very Hot
	Flag2GlideMode               uint64 = 1 << 12 // Glide Mode
	Flag2OnFootInHangar          uint64 = 1 << 13 // On Foot in Hangar
	Flag2OnFootSocialSpace       uint64 = 1 << 14 // On Foot Social Space
	Flag2OnFootExterior          uint64 = 1 << 15 // On Foot Exterior
	Flag2BreathableAtmosphere    uint64 = 1 << 16 // Breathable Atmosphere
	Flag2TelepresenceMulticrew   uint64 = 1 << 17 // Telepresence Multicrew
	Flag2PhysicalMulticrew       uint64 = 1 << 18 // Physical Multicrew
	Flag2FsdHyperdriveCharging   uint64 = 1 << 19 // FSD Hyperdrive Charging
)

// Elite Dangerous Status.json GuiFocus constants.
const (
	GuiFocusNoFocus         = 0
	GuiFocusInternalPanel   = 1  // Right hand side panel (modules, status)
	GuiFocusExternalPanel   = 2  // Left hand side panel (navigation, transactions)
	GuiFocusCommsPanel      = 3  // Top panel (comms)
	GuiFocusRolePanel       = 4  // Bottom panel (helm, fighter, SRV)
	GuiFocusStationServices = 5
	GuiFocusGalaxyMap       = 6
	GuiFocusSystemMap       = 7
	GuiFocusOrrery          = 8
	GuiFocusFSS             = 9
	GuiFocusSAA             = 10 // Detailed surface scanner
	GuiFocusCodex           = 11
)
