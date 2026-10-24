// Generated from actual game types.json; do not maintain layouts by hand.
#pragma once
#include <cstddef>
#include <cstdint>
namespace abi {
namespace Input {
constexpr uint32_t hash=0x9621e4c3;
constexpr size_t size=1536;
constexpr size_t data=0;
constexpr size_t adminCommands=952;
constexpr size_t toggleConfig=1432;
constexpr size_t doubleInputBindings=1440;
constexpr size_t inputMode=1441;
constexpr size_t clientOnlyPlayerInput=1444;
constexpr size_t uiPlayerInput=1504;
}
namespace InputData {
constexpr uint32_t hash=0x867d938b;
constexpr size_t size=952;
constexpr size_t inventoryTransferAction=0;
constexpr size_t camera=32;
constexpr size_t actionbarSelectionAction=96;
constexpr size_t sortInventoryAction=104;
constexpr size_t selectSongAction=112;
constexpr size_t loreVoiceoverAction=120;
constexpr size_t inventoryStockCycleAction=152;
constexpr size_t buildingStockCycleAction=160;
constexpr size_t playerCraftingAction=180;
constexpr size_t areaDepositPermissionAction=248;
constexpr size_t upgradeStorageAction=260;
constexpr size_t changeCustomizationAction=320;
constexpr size_t teleportAction=364;
constexpr size_t unlockSkillAction=376;
constexpr size_t triggerKnowledgeAction=388;
constexpr size_t setPersonalProgressionAction=404;
constexpr size_t selectDialogOptionAction=412;
constexpr size_t stopDialogOptionAction=424;
constexpr size_t npcInputAction=436;
constexpr size_t changeEquipmentAction=448;
constexpr size_t swapEquipmentSetAction=464;
constexpr size_t itemAction=476;
constexpr size_t createBuildingItemAction=500;
constexpr size_t createItemAction=512;
constexpr size_t flameAltarAction=524;
constexpr size_t logicEntityConfigAction=536;
constexpr size_t waypointAction=568;
constexpr size_t weaponGemAction=600;
constexpr size_t parryAction=628;
constexpr size_t accountAction=648;
constexpr size_t redeemItemAction=672;
constexpr size_t triggerEmoteAction=688;
constexpr size_t movementStick=696;
constexpr size_t movementStickInputMode=704;
constexpr size_t cameraRotationInput=708;
constexpr size_t targetSelectionInput=748;
constexpr size_t cursorInput=760;
constexpr size_t mask=928;
constexpr size_t digitalInput=936;
constexpr size_t isRespawnChargedFully=944;
}
namespace ClientCamera {
constexpr uint32_t hash=0xd7408807;
constexpr size_t size=64;
constexpr size_t position=0;
constexpr size_t orientation=24;
constexpr size_t distance=40;
constexpr size_t fovY=44;
constexpr size_t aspectRatio=48;
constexpr size_t nearPlane=52;
constexpr size_t farPlane=56;
}
namespace Transfer {
constexpr uint32_t hash=0x71b749c8;
constexpr size_t size=32;
constexpr size_t versionData=0;
constexpr size_t sourceEntityId=4;
constexpr size_t targetEntityId=8;
constexpr size_t sourceSlotId=12;
constexpr size_t targetSlotId=20;
constexpr size_t type=28;
constexpr size_t flags=29;
constexpr size_t amount=30;
}
namespace WorldTransform {
constexpr uint32_t hash=0x027ac564;
constexpr size_t size=56;
constexpr size_t transform=0;
}
namespace WorldPosition {
constexpr uint32_t hash=0xf99c5535;
constexpr size_t size=24;
constexpr size_t x=0;
constexpr size_t y=8;
constexpr size_t z=16;
}
namespace Pickup {
constexpr uint32_t hash=0x08775a90;
constexpr size_t size=28;
constexpr size_t itemId=0;
constexpr size_t item=4;
constexpr size_t level=20;
constexpr size_t rarity=24;
constexpr size_t pickupAmount=26;
}
namespace Inventory {
constexpr uint32_t hash=0xb19528a9;
constexpr size_t size=96;
constexpr size_t slots=0;
}
namespace InventorySetup {
constexpr uint32_t hash=0xc24df82a;
constexpr size_t size=84;
constexpr size_t linksEntities=0;
constexpr size_t linksCategories=64;
constexpr size_t inventoryFlags=80;
constexpr size_t genericSlotCount=81;
constexpr size_t availableSlotCount=82;
constexpr size_t isInitialized=83;
}
namespace Offer {
constexpr uint32_t hash=0x8598ad86;
constexpr size_t size=12;
constexpr size_t verbId=0;
constexpr size_t lastAcceptionId=4;
constexpr size_t isOffered=8;
}
namespace Stack {
constexpr uint32_t hash=0x36021ddf;
constexpr size_t size=12;
constexpr size_t id=0;
constexpr size_t data=4;
}
namespace ItemData {
constexpr uint32_t hash=0x81063905;
constexpr size_t size=8;
constexpr size_t count=0;
constexpr size_t pide=4;
}
// Current generated type registry: SlotSelection.actionbarEntityIds is EntityId[2].
namespace SlotSelection {
constexpr uint32_t hash=0x34bc3e2f;
constexpr size_t size=216;
constexpr size_t actionbarEntityIds=160;
}
namespace PlayerState {
constexpr uint32_t hash=0x6ff967bb;
constexpr size_t size=96;
constexpr size_t previousActorState=0;
constexpr size_t lastSprintEndTime=8;
constexpr size_t lastBlockEndTime=16;
constexpr size_t noMovementInputDuration=24;
constexpr size_t outsideSafeZoneCountdown=32;
constexpr size_t gliderActivationTime=40;
constexpr size_t sprintTurnSpeedFactor=48;
constexpr size_t playerStateMask=52;
constexpr size_t dungeonZoneMapMarkerId=56;
constexpr size_t rangedWeaponArmedViaInput=60;
constexpr size_t parryProjectile=64;
constexpr size_t beamEntityId=76;
constexpr size_t buildingSparkEntityId=80;
constexpr size_t currentAimYawAngleOffset=84;
constexpr size_t currentAimPitchAngleOffset=88;
constexpr size_t airborneMeleeAttacksCount=92;
constexpr size_t deepSubmergeMaterialJumpCount=93;
}
namespace NetworkHealth {
constexpr uint32_t hash=0x216eb5f9;
constexpr size_t size=8;
constexpr size_t health=0;
constexpr size_t healthMax=4;
}
namespace FamilyInfo {
constexpr uint32_t hash=0x862fb5b1;
constexpr size_t size=1;
constexpr size_t family=0;
}
namespace BaseActor {
constexpr uint32_t hash=0x4c56de7b;
constexpr size_t size=3104;
constexpr size_t currentAction=0;
constexpr size_t currentState=3064;
constexpr size_t currentAbilityMask=3072;
constexpr size_t sequenceRuntimeId=3080;
}
namespace ClientActor {
constexpr uint32_t hash=0x3b912d39;
constexpr size_t size=3128;
constexpr size_t lastReceivedSequenceRuntimeId=3104;
}
namespace NetworkActor {
constexpr uint32_t hash=0x1a146dec;
constexpr size_t size=208;
constexpr size_t sequenceRuntimeId=0;
constexpr size_t currentAbilityMask=24;
constexpr size_t currentState=32;
constexpr size_t setStateMask=40;
constexpr size_t unsetStateMask=48;
constexpr size_t interactionHostId=56;
constexpr size_t currentActionEntityTags=72;
constexpr size_t usedItemId=200;
constexpr size_t triggerType=204;
constexpr size_t currentActionState=206;
}
namespace ActionSequence {
constexpr uint32_t hash=0x8e896538;
constexpr size_t size=3064;
constexpr size_t triggerContext=0;
constexpr size_t playbackContext=368;
}
namespace Playback {
constexpr uint32_t hash=0x9880b510;
constexpr size_t size=2696;
constexpr size_t subSequenceId=0;
constexpr size_t playedTime=8;
constexpr size_t currentTime=16;
constexpr size_t sequenceLength=24;
constexpr size_t scopeStartTime=32;
constexpr size_t scopeEndTime=40;
constexpr size_t scopeAbortMask=48;
constexpr size_t scopeKeepAliveMask=56;
constexpr size_t scopeJumpToScopeEndOnAbort=64;
constexpr size_t state=65;
constexpr size_t playbackSpan=72;
constexpr size_t followUps=104;
constexpr size_t abilityMask=2648;
constexpr size_t setStateMask=2656;
constexpr size_t unsetStateMask=2664;
constexpr size_t sequenceAbortMask=2672;
constexpr size_t sequenceKeepAliveMask=2680;
constexpr size_t wrapedAround=2688;
}
namespace SequenceRuntime {
constexpr uint32_t hash=0xd049ea63;
constexpr size_t size=24;
constexpr size_t sequenceId=0;
constexpr size_t triggerTime=8;
constexpr size_t actorEntityId=16;
constexpr size_t callIndex=20;
}
namespace BaseAnimation {
constexpr uint32_t hash=0xc835aae0;
constexpr size_t size=568;
constexpr size_t initialAnimationStateIdReference=0;
constexpr size_t animationInputs=16;
constexpr size_t animationRootMotionMask=552;
constexpr size_t animationStateId=556;
constexpr size_t retriggerCount=560;
constexpr size_t overlayRingbufferIndex=564;
}
namespace ClientAnimation {
constexpr uint32_t hash=0xce4bea52;
constexpr size_t size=592;
constexpr size_t clientOnlyOverlayInputs=568;
}
namespace NetworkAnimation {
constexpr uint32_t hash=0xf195e6fb;
constexpr size_t size=568;
}
namespace ServerActor {
constexpr uint32_t hash=0x009e8b34;
constexpr size_t size=3832;
constexpr size_t currentServerAction=3104;
constexpr size_t deathSequence=3736;
constexpr size_t deathSequenceWaitsForGroundedState=3752;
constexpr size_t defaultVisibleState=3753;
constexpr size_t sendDeathEvent=3754;
constexpr size_t allowDeathSequence=3755;
constexpr size_t deathSequenceId=3756;
constexpr size_t currentColliderMode=3760;
constexpr size_t rotationInterpolation=3764;
}
namespace NetworkVelocity {
constexpr uint32_t hash=0x3958b585;
constexpr size_t size=16;
constexpr size_t speed=0;
constexpr size_t direction=4;
}
namespace ModelResource {
constexpr uint32_t hash=0x192b2ed8;
constexpr size_t size=16;
constexpr size_t model=0;
}
namespace VoxelResource {
constexpr uint32_t hash=0x728ff2b9;
constexpr size_t size=20;
constexpr size_t model=0;
constexpr size_t materialOverride=16;
}
namespace ModelDynamic {
constexpr uint32_t hash=0x29a43465;
constexpr size_t size=32;
constexpr size_t lastModel=0;
constexpr size_t newModel=16;
}
namespace ItemState {
constexpr uint32_t hash=0x3a2fac75;
constexpr size_t size=20;
constexpr size_t containedInSlotId=0;
constexpr size_t itemId=8;
constexpr size_t baseDamageUi=12;
constexpr size_t itemState=14;
constexpr size_t itemRarityUi=16;
constexpr size_t itemLevelUi=17;
}
namespace NetworkGrowth {
constexpr uint32_t hash=0x5a8aacc0;
constexpr size_t size=2;
constexpr size_t voxelMaterialId=0;
constexpr size_t customGrowthLimit=1;
}
namespace NetworkSelectedTargets {
constexpr uint32_t hash=0xc384ef24;
constexpr size_t size=264;
constexpr size_t targetIds=0;
constexpr size_t combatTargetSupportsHardLock=256;
constexpr size_t combatTargetImportance=257;
}
namespace GameObjectId {
constexpr uint32_t hash=0x0aa3adef;
constexpr size_t size=16;
constexpr size_t value=0;
constexpr size_t type=8;
}
namespace UiInteractable {
constexpr uint32_t hash=0x92cfc913;
constexpr size_t size=12;
constexpr size_t handle=0;
constexpr size_t verbId=4;
constexpr size_t hintRange=8;
}
namespace ClientLineOfSightState {
constexpr uint32_t hash=0x4096d385;
constexpr size_t size=1;
constexpr size_t lineOfSightState=0;
}
namespace CollisionFeedback {
constexpr uint32_t hash=0x38349d86;
constexpr size_t size=44;
constexpr size_t material=0;
constexpr size_t itemId=16;
constexpr size_t itemRef=20;
constexpr size_t propDamageSusceptibility=36;
constexpr size_t materialId=40;
}
namespace Ability {
constexpr uint32_t CanInteract=1u<<11;
}
}
