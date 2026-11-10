// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"
#include "UI/VBUILayerTags.h"

namespace VBGameplayTags
{
	// Ability
	// 공격 계열의 부모 태그 - 개별 공격 GA 의 AssetTags 는 이 아래 leaf 를 단다
	// 전투 규칙 표(DA_CombatRules)가 공격 전부를 이 하나로 가리킨다 - AreAbilityTagsBlocked 가 부모까지 확장해 비교한다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Light);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Riposte);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_CinematicSkill);
	// 띄운 적을 쫓아 올라가는 공중 콤보(UVBGA_AirCombo).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Air);
	// 적 근접 공격(UVBGA_EnemyMeleeAttack). 플레이어 경공격과 이름을 나눠야 전투 규칙 표가 둘을 따로 막을 수 있다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Enemy);
	// 부모 태그. Dodge, Guard 를 한 번에 막을 때 쓴다 - 엔진이 후보 GA 의 AssetTags 를 부모까지
	// 확장해 비교하므로 이 하나로 Ability.Defense.* 전부가 걸린다(Ability.Attack 과 같은 수단).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Defense);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Defense_Dodge);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Defense_Guard);
	// 방패 가드(한손검). 전투 규칙 표가 이 아래에만 Passive.Parry 를 요구한다 - Fighter 가드는 부모 태그라 걸리지 않는다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Defense_Guard_Shield);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Medical_Purify);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Medical_Diagnose);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Medical_Execute);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Locomotion_Crouch);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Locomotion_Sprint);
	// UVBGA_Interact 의 AssetTag. 다른 GA 가 상호작용을 막거나 끊을 때 가리키는 이름이다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Interact);
	// UVBGA_UseItem 의 AssetTag. 다른 GA 가 아이템 사용을 막거나 끊을 때 가리키는 이름이다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Item_Use);
	// 피격 반응 GA 들의 부모. 처형 창이 열릴 때 이 이름으로 반응 전부를 끊는다(UVBGA_FinisherWindow::OpenWindow).
	//   leaf 가 따로인 이유: 전투 규칙 표의 줄은 능력 하나를 가리킨다 - 경직과 넘어짐은 막는 것이 다르다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Reaction);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Reaction_HitReact);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Reaction_Stagger);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Reaction_Knockdown);
	// 쓰러진 뒤 일어나기(처형을 버틴 적 UVBGA_GetUp, 플레이어 래그돌 회복 UVBGA_PlayerRagdoll).
	//   반응 아래에 두지 않는다 - 처형 창의 반응 끊기에 일어나기가 같이 끊기면 안 된다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Recovery_GetUp);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Recovery_Ragdoll);
	// 처형 창(UVBGA_FinisherWindow)과 무기 소환(UVBGA_WeaponSummon)의 이름. 표가 이 능력들을 가리킬 때 쓴다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Combat_FinisherWindow);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Weapon_Summon);

	// State
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Locomotion_Crouching);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Locomotion_Sprinting);
	// 트래버설 세션 상태. UVBTraversalComponent::SetTraversalState 만 쓴다(붙은 면 셋, 한 번짜리 동작, 올라서기).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Traversal_Hang);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Traversal_Ladder);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Traversal_Climb);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Traversal_Move);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Traversal_Exit);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_InCombat);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Attacking);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Attacking_Light);
	// 내가 띄운 적을 쫓을 수 있는 동안(복제 루즈 태그). 있으면 공격 입력이 경공격 대신 공중 콤보다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_AirOpening);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_AirRecovery);
	// 공중 콤보 중(쫓기, 공중 연타).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Attacking_Air);

	// 패링 성공 직후 열리는 반격 창. 이 태그가 있는 동안에만 UVBGA_Riposte 가 활성화된다.
	// 경공격은 State.Combat.Guarding 으로 차단돼 있어 둘이 같은 입력을 써도 경합이 성립하지 않는다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_RiposteWindow);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Dodging);
	// 회피 커밋: 회피 시작부터 AttackCancelSeconds 까지 공격이 롤을 끊지 못한다. 그 사이 공격 입력은 캐릭터가 저장했다가 커밋이 풀리면 내보낸다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_DodgeCommit);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Staggered);
	// 그로기(다운) - 넘어짐, 대기, 기상 동안. UVBGA_Knockdown 이 유일한 생산자(루즈태그, 서버)
	// Staggered 와 함께 붙고(BT 는 Staggered 로 닫힌다) 이 태그는 지금 넘어져 있다는 것만 말한다
	// 소비자 = 다운 중 사망(사망 몽타주 생략), 다운 중 반격 GA(경직 취소 감시 생략)
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Downed);
	// 히트리액트(플린치) 재생 중 - UVBGA_HitReact 의 ActivationOwnedTags. Staggered(Poise 파탄)와 달리 맞은 그 순간이다
	// 소비자 = BT_Infected 의 이동 분기 게이트(맞는 동안 걸어 나가지 않게)
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_HitReacting);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Guarding);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_SuperArmor);
	// 깨지는 아머(UVBAttackConfig::bArmorBreakable) - 자식 태그라 SuperArmor 소비자는 그대로 걸리고, poise hub 만 둘을 가른다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_SuperArmor_Breakable);
	// 보스 페이즈 전환 몽타주가 도는 중. AVBBossBase 가 단독 생산자(로스태그). 소비자 둘 - BT_Infected 의 이동, 공격 분기
	//   (Staggered, HitReacting, FinisherWindow 와 같은 반전 데코레이터 줄)와 전투 규칙 표의 이 상태 줄(적 공격 막기).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_PhaseTransition);
	// 조우가 보류 중이다(시네마틱 장면 동안). UVBEncounterGateComponent 가 단독 생산자(로스태그).
	//   소비자 = 전투 규칙 표의 이 상태 줄(적 공격 막기). 행동 트리 멈춤은 AI 컨트롤러가 같은 문의 신호로 한다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Encounter_Held);
	// 처형 창이 열려 있다 - 이 태그를 든 대상만 처형, 정화의 대상이 된다
	// UVBGA_FinisherWindow 가 반응이 끝나 창이 열릴 때 붙인다. 시한부 창이라 체력이 문턱 아래여도 창이 닫혀 있으면 없다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_FinisherWindow);
	// 처형 창 GA 가 켜져 있다 - 쓰러진 캐릭터가 일어나기를 기다리는 중이거나 이미 열렸다. UVBGA_FinisherWindow 의 ActivationOwnedTags(단독 생산자).
	//   읽는 곳은 UVBAbilitySystemStatics::IsFinisherWindowArmed 하나를 거친다. 소비자 = 경직 GA 의 기절 게이지 소진 거절,
	//   기절 게이지 세트의 게이지 피해 거절, 회복 멈춤, AI 의 포커스 게이트.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_FinisherWindowArmed);
	// 이 적의 처형 창은 기절 게이지가 다 차면 열린다. 창 능력이 부여될 때 붙이고 회수될 때 뗀다(UVBGA_FinisherWindow, 서버, 클라 각자, 비복제).
	//   읽는 곳 = 경직 GA(그 파탄을 받지 않는다)와 피해 허브(체력 문턱 창을 묻지 않는다).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_PostureExecutable);
	// 체력 문턱으로 켜진 처형 창 - 처형 아닌 체력 피해를 받지 않는다(UVBAbilitySystemStatics::ShouldIgnoreHit). 창 GA 가 단독 생산자다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_FinisherWindowImmune);

	// 처형을 거는 쪽의 상태(FinisherWindow 의 짝) - UVBGA_Execute 의 ActivationOwnedTags 라 태그 수명이 곧 장면 수명이다
	// 소비자 = 피격 반응, 경직의 자기 차단, 피해 계산의 무적, 비-GA 진입점(무기 교체, 트래버설)의 IsActorFinishing
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Finishing);
	// 시네마틱 스킬(UVBGA_CinematicSkill) 연출이 도는 중
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_CinematicSkill);

	// 처형을 버티고 일어나는 중. UVBGA_GetUp 이 단독 생산자다(ActivationOwnedTags).
	//   이 동안 공격을 막는다 - 일어나자마자 때릴 수 있으면 처형을 버틴 것이 그냥 이득이 된다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_GettingUp);
	// 래그돌 중(플레이어 UVBPlayerRagdollComponent, 적 AVBEnemyBase 의 회복형 래그돌). 무엇을 막는지는 전투 규칙 표에 있다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Ragdoll);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_LockedOn);
	// 공격 예고 중. 스윙이 나가기 전 UVBEnemyConfig::TelegraphDuration 동안 유지된다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Telegraphing);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Medical_Infected);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Medical_Purifying);
	
	// 무기 State
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed_Blade);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed_BigSword);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed_Fighter);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed_Magic);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Armed_OneHandSword);

	// 전투 상태
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Combat_Exiting);
	
	// 무기 동작
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Weapon_Summoning); // 소환
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Weapon_Sheathing); // 수납
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Weapon_TempSheathed); // 파쿠르 임시 수납

	// 상호작용 중. UVBGA_Interact 가 붙인다. 무기 교체 같은 GA 밖 기능이 지금 상호작용 중인지 묻는다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Interacting);
	// 스마트 오브젝트 사용 중. UVBGASPSmartObjectComponent 가 붙인다. 무엇을 막는지는 전투 규칙 표에 있다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Interacting_SmartObject);
	

	// Effect
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Effect_Damage_Physical);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Effect_Damage_Infection);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Effect_Purify);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Effect_Execute);

	// Event
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_Hit);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_Death);
	// 예고 시작 에지. 비주얼/사운드가 붙는 훅이자 미래의 회피 보조, 경고 UI 의 입력점.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_TelegraphStart);
	// Poise 가 0에 도달. State.Combat.PostureExecutable 을 든 적은 처형 창이 받고, 아니면 UVBGA_Stagger 가 받는다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_PoiseBroken);
	// 적이 처형할 수 있게 됐다(게임 사건 버스, 서버) - 튜토리얼이 처형 키 안내를 여기서 건다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_ExecuteAvailable);
	// 실효 등급이 넘어뜨림 이상인 타격 - 허브가 Event.Combat.Hit 보다 먼저 쏘고 UVBGA_Knockdown 이 받는다
	// Hit 과 따로인 이유: 넘어짐 GA 가 먼저 Downed 를 붙여야 뒤따르는 Hit 의 플린치가 막힌다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_KnockedDown);
	// 맞은 쪽이 띄워졌다(서버). 맞은 쪽의 다운 GA 가 때린 쪽에게 보낸다 - Target = 띄워진 캐릭터.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_LaunchedTarget);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_AirSlam);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_AirHold);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_AirRelease);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_AirSlam);

	// 처형을 맞았는데 죽지 않았다. UVBGA_Execute 가 장면 끝에 쏘고 UVBGA_GetUp 이 받는다.
	//   OptionalObject 에 기상 몽타주를 싣는다 - 클립이 처형마다 짝이라(카타나 SSH / 대검 2H)
	//   피해자가 「어떤 처형에 맞았는지」를 알 필요 없이 맞는 클립을 받는다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_FinisherSurvived);
	// 체력 문턱 창 요청 - 창 설정이 HealthThreshold 인 적에게만 VBDamageResponse 가 쏘고 UVBGA_FinisherWindow 가 받는다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combat_FinisherWindow);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Medical_Cured);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Montage_AttackTrace);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combo_WindowOpen);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combo_WindowClose);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Combo_InputReceived);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Ability_InputActivate);

	// Input
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Light);
	// 적 슈퍼아머 반격 GA 의 InputTag - 플레이어 입력이 아니라 BT 가 고르는 이름이다
	// UVBEnemyConfig::ArmoredCounterAttackInputTag 가 가리키고, 연속 피격 조건이 맞으면 GetAttackInputTag 가 돌려준다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Armored);
	// 보스 페이즈 2 의 공격 GA 태그 - 페이즈 1 은 Input_Attack_Light 를 쓰고, 2 는 같은 ASC 안에서 갈라야 해서 따로 둔다
	// Input.* 태그는 전부 네이티브라(GA 생성자가 기본 InputTag 를 상수로 든다) 이것도 ini 가 아니라 여기 둔다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Boss_Phase2);
	// 보스가 넘어진 채 휘두르는 반격 GA 의 태그. BT 가 아니라 UVBGA_Knockdown 이 켠다(BT 는 Staggered 로 닫혀 있다).
	// 페이즈 태그와 같은 이유로 네이티브다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Attack_Boss_Down);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Combat_Guard);
	// 시네마틱 특수 스킬(Q / L3+R3) - 부여된 GA 의 InputTag
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_CinematicSkill);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Dodge);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Purify);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Execute);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Crouch);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Sprint);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Aim); // 어빌리티가 아니다 - 누르고 있기/토글 설정의 열쇠로만 쓴다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_LockOn);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_SwitchTarget);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_WeaponSummon);
	// 환경 상호작용(문, 레버 등). UVBGA_Interact 의 InputTag. 탭과 홀드가 같은 입력이고 갈림은 대상 설정이 정한다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Interact);
	// 영웅이 직접 처리하는 입력의 열쇠(능력 입력이 아니다) - 입력 설정 자산의 Native 줄이 이 태그로 입력 행동을 찾는다
	// 목록은 UVBInputConfig::GetNativeInputContract 하나다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Move);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Look);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Jump);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_WalkToggle);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_WeaponSlot_1);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_WeaponSlot_2);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_WeaponSlot_3);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Ragdoll);

	// AI
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_State_Idle);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_State_Combat);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_State_Dead);
	
	// DATA (SetByCaller용)
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_ReputationChange);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_MedicalAmount);
	// 넉백 세기(cm/s) - 공격자 스탠스값을 데미지 스펙에 실어 피격자 hub(PostGameplayEffectExecute)로 전달
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_KnockbackStrength);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_KnockbackZ);
	// 이 타격이 대상 Poise 에서 깎는 양 - 넉백 세기와 같은 데미지 스펙에 실어 보낸다
	// 미는 방향, 반응 등급, 띄움 높이는 타격의 사실이라 FVBGameplayEffectContext 가 나른다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_PoiseDamage);

	// Combat.Impact - 받는 쪽 저항까지 반영해 허브가 판정한 실효 반응 등급, Event.Combat.Hit 의 InstigatorTags 에 하나만 실린다
	// 태그가 없는 이벤트(패링 되받기 등)는 가벼움으로 읽는다(VBHitImpactFromTags)
	// None 은 저항이 등급을 다 깎았다는 뜻이라 태그 없음(가벼움)과 따로 둔다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_None);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_Light);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_Heavy);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_Knockdown);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Impact_Launch);

	// Cooldown
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Purify);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Execute);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Attack_Light);
	// 적 슈퍼아머 반격의 쿨다운 태그. 보통 공격(Light)과 다른 태그여야 둘이 서로를 막지 않는다 - 반격만 길게 쉰다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Attack_Armored);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Dodge);
	// 처형 창의 재무장 쿨다운. 창이 닫힌 뒤 이만큼은 다시 안 열린다 - 없으면 약한 연타가 적을 영구 무력화한다.
	// 이 계열 태그들과 마찬가지로 C++ 소비자가 없다. GE 자산이 이름으로 들고 엔진의 CheckCooldown 이 읽는다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_FinisherWindow);
	
	// GameplayCue
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Damage_Physical);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Damage_Physical_Heavy);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Damage_Blunt);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Damage_Blunt_Heavy);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Damage_Bleed_Burst);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Finisher_Strike);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_State_Staggered);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_State_Death);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_HitStop);
	// 히트스톱과 같은 Combat 가지에 둔다 - 둘 다 '타격 게임필' 축이고, 데미지 종류(Damage.*)와는 다른 축이다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_HitFlash);
	// 서 있는 캐릭터의 물리 피격 레이어(UVBGC_HitPhysics). 발광과 따로 - 읽는 쪽과 끄는 조건이 다르다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_HitPhysics);

	// 처형 창이 열린 동안 대상 메시에 거는 지속 발광. UVBGA_FinisherWindow 가 켜고 끈다.
	//   루프 큐인 이유: 발광은 보는 모든 사람에게 보여야 하는데 능력 본문은 서버에서만 돈다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_FinisherWindow);
	// 방향성 임팩트 쉐이크 - 히트스톱, 히트플래시와 같은 Combat 가지
	// 무기별로 쪼개지 않는다 - 무기 차별화는 프로필 자산의 ShakeClass 가 맡는다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_SwingImpact);
	// 적 공격 예고 Add/Remove 큐 - 예고 시작에 붙고 스윙 시작, 취소에 떨어진다
	// 답(Attack.Answer.*)은 AggregatedSourceTags, 예고 길이는 RawMagnitude 로 싣는다
	// 답마다 큐 태그를 쪼개지 않는다 - 색, 소리 차별화는 UVBTelegraphConfig 의 행이 맡는다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_Telegraph);
	// 정확한 패링의 화면 보상(슬로우 + 퍼지는 울렁임). 데미지 허브의 패링 분기만 쏜다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_Parry);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Combat_GuardBlock);

	// Attack.Answer - 그 공격에 무엇으로 답할 수 있는가(UVBAttackConfig::AttackAnswer). 예고 큐와 데미지 허브가 같은 값을 읽는다
	// Parryable = 가드, 패링이 통한다(흰색), DodgeOnly = 가드를 무시하고 회피 무적만 통한다(빨강)
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attack_Answer_Parryable);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attack_Answer_DodgeOnly);

	// UI.Slot.HUD - HUD 의 UI 슬롯 자리(UVBUISlotWidget) 이름. 자리, 기본 위젯은 배치 데이터(UVBHUDLayoutData)가 정한다.
	// 네이티브로 두는 이유: 태그 오타가 화면에 아무것도 안 뜨는 것으로만 드러나지 않게 컴파일, 저작 검사로 막는다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_WeaponWheel);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_QuestTracker);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_Progression);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_QuickSlots);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_ChapterCard);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_RegionBanner);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_TutorialHint);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_WorldIndicators);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_Subtitle);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_TouchControls);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_BossBar);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_Minimap);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_CinematicSkill);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Slot_HUD_Stamina);

	// 처형 프롬프트의 표시 요청 묶음(UVBExecutePromptComponent 가 등록한다). 표시기 층이 이 묶음을 그릴 위젯을 고른다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Indicator_Group_Execute);

	// UI.Screen - 화면을 여는 쪽이 부르는 이름. 층, 위젯은 UVBUISettings::Screens 표가 정한다.
	//  코드가 직접 여는 화면만 네이티브로 둔다. 데이터(대화 효과 등)만 여는 화면은 ini 태그로 충분하다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_HUD);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Pause);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_GameOver);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_MainMenu);

	// Melee.Shape - 무기 궤적 판정이 어느 날로 쓸지. 모양 자체는 공격자의 UVBMeleeTraceComponent 가 든다
	// Weapon = 지금 든 무기, 나머지 = 뼈 기준(맨손, 발차기, 무기 없는 적)
	// 부모 태그는 공격 GA 가 이벤트 태그 묶음에서 모양을 골라낼 때 쓴다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_Weapon);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_HandL);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_HandR);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_FootL);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_FootR);
	// 내려찍기용 공격자 앞 작은 범위 (root 위의 구, BaseLocal == TipLocal)
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Shape_Slam);
	// 판정 창 이벤트를 표시하는 태그 - 창 시작도 Event.Montage.AttackTrace 로 가므로 한 프레임 노티와 구분하는 데 쓴다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee_Window);
	// 판정 창 닫힘. UVBANS_AttackTraceWindow::NotifyEnd 가 보낸다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Montage_AttackTraceEnd);
	// 던지기 몽타주의 놓는 시각. UVBGA_BossThrow 가 기다렸다가 투사체를 띄운다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Montage_ProjectileRelease);

	// 아이템 - 사건은 UVBGameEventSubsystem 으로 가고 대상 태그 = 아이템 ItemId. 퀘스트 목표가 이것을 듣는다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Acquired);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Used);
	// 사용 동작 - 위 둘(게임 사건)과 달리 GAS 게임플레이 이벤트다
	// Use = 인벤토리가 폰 ASC 로 보내는 시작 요청(UVBGA_UseItem 의 트리거, OptionalObject = 아이템). UseMarker = 몽타주 표식 노티가 보내는 확정 시각.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Use);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_UseMarker);
	// 장비 칸 종류 다섯 - 칸 배치는 UVBEquipmentComponent::SlotLayout 데이터가 정한다
	// Equipped, Unequipped 는 게임 사건(대상 = ItemId)이라 퀘스트가 무엇을 입었는지 듣는다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Slot_Head);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Slot_Body);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Slot_Arms);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Slot_Legs);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_Slot_Charm);

	// 외형 칸 - 캐릭터 메시 위에 끼우는 조각 하나가 차지하는 자리. 장비 칸과 다르다(대응 표 UVBAppearanceSettings).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Slot);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Slot_Head);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Slot_Top);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Slot_Bottom);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Slot_Hands);
	// 신체 부위 - 마지막 이름이 바디 메시의 머티리얼 슬롯 이름과 같다(세 바디 모두 이 일곱). 조각이 덮는 부위를 숨긴다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Section);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Section_Head);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Section_Hair);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Section_Hands);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Section_TorsoUpperArms);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Section_Forearms);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Section_HipsLegs);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Appearance_Section_Feet);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Equipped);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Item_Unequipped);
	// 성장 - 게임 사건(UVBGameEventSubsystem). LevelUp 의 수 = 오른 레벨 수, SkillUnlocked 의 대상 = 연 칸(Skill.Node.*).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Progression_LevelUp);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Progression_SkillUnlocked);
	// 스킬 트리 화면(UVBSkillTreeScreenWidget). 층, 클래스는 UVBUISettings::Screens.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_SkillTree);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Inventory);
	// 치유량 SetByCaller(GE_Heal). 치유량은 GE 를 거는 쪽의 데이터(아이템 사용 효과 등)가 정한다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Heal);
	// 손에 든 무기의 강화 배수 SetByCaller(GE_WeaponEnhancement). 값은 강화 컴포넌트가 관리하고, 거는 쪽은 무기 상태다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_WeaponDamageScale);

	// 대화 - 노드의 ReachedTag 가 있으면 이 사건의 대상 태그로 방송된다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Dialogue_NodeReached);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Dialogue);

	// 퀘스트 - 사건은 UVBGameEventSubsystem 으로 간다 - 퀘스트 일지가 듣는다. 대상 태그는 각 주석.
	// 적 처치. 대상 = 죽은 적의 UVBEnemyConfig::EnemyTags.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Enemy_Killed);

	// 메시지 버스 채널(UVBGameplayMessageSubsystem). 위의 Event.* 는 규칙 입력(UVBGameEventSubsystem)이고 이것은 일반 알림이다.
	// 모든 캐릭터의 죽음(서버). 내용 = FVBDeathFacts. 보내는 곳 = UVBHealthComponent, 사망 단계가 끝난 뒤.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Message_Death);
	// 캐릭터가 타격을 받았다(서버, 막은, 패링한 타격 포함). 내용 = FVBHitReceivedMessage. 보내는 곳 = UVBHealthAttributeSet, 가드 판정 뒤.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Message_Combat_HitReceived);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Message_Combat_ChargeCredit);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_CinematicSkillGaugeGain);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_CinematicSkill_Ready);
	// 상호작용 완료. 대상 = 그 대상의 UVBInteractableComponent::EventTags(비면 방송 안 함).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Interaction_Completed);
	// 영역 진입. 대상 = AVBAreaTrigger::EventTags(비면 방송 안 함).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Area_Entered);
	// 퀘스트 자신의 진행. 대상 = QuestId.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Quest_Started);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Quest_StageAdvanced);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Quest_Completed);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Journal);
	// 상점 화면. 여는 쪽 = AVBPlayerController(상점 컴포넌트의 보기를 듣는다).
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Shop);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Crafting);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_FastTravel);
	// 키 다시 지정 화면. 여는 쪽 = 옵션 화면의 조작 탭. Modal 층.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_KeyBindings);
	// 옵션 화면. 여는 쪽 = 메인, 일시정지 메뉴의 설정. Modal 층.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Options);
	// 도감 화면. 여는 쪽 = AVBPlayerController::RequestCollectionScreen. Menu 층.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Collection);
	// 튜토리얼 다시 보기 화면. 여는 쪽 = AVBPlayerController::RequestTutorialScreen(일시정지 안내). Modal 층.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Tutorial);
	// 새 게임 난이도 고르기. 여는 쪽 = AVBMenuPlayerController::RequestNewGame. Modal 층 - 아래 메뉴, 슬롯 화면을 그대로 둔다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_DifficultyPicker);
	// 이 화면은 UVBUIMessagingSubsystem 만 Dialog 층에 넣는다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Screen_Confirmation);

	// 튜토리얼 정의 ID 의 뿌리(Tutorial.*, 단계 ID 는 Tutorial.Step.*). 정의 = UVBTutorialDefinition.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tutorial);

	// 능력 발동 실패 이유 - 사용자에게 보이는 문구는 알림 데이터 자산이 가진다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fail_Cooldown);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fail_Cost);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fail_Networking);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fail_TagsBlocked);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fail_TagsMissing);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fail_NoMedicalTarget);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fail_NoOwnedWeapon);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Fail_Unknown);

	// 귀속 아이템으로 얻는 패시브(UVBPassiveComponent 만 ASC 에 단다). 전투 규칙 표가 능력마다 요구한다.
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Passive);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Passive_Dodge);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Passive_Parry);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Passive_Riposte);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Passive_Execute);
}
