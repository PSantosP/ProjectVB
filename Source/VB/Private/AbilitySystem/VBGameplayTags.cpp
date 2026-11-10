// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBGameplayTags.h"

namespace VBGameplayTags
{
	// Ability
	// 공격 계열 부모. 경직(UVBGA_Stagger)의 BlockAbilitiesWithTag 가 이 하나로 공격 leaf 전부를 막는다.
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack, "Ability.Attack");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_Light, "Ability.Attack.Light");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_Riposte, "Ability.Attack.Riposte");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_CinematicSkill, "Ability.Attack.CinematicSkill");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_Air, "Ability.Attack.Air");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_Enemy, "Ability.Attack.Enemy");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Defense, "Ability.Defense");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Defense_Dodge, "Ability.Defense.Dodge");
	// 가드 GA 의 AssetTag - 무기 교체 때 CancelAbilitiesWithTag 로 가드를 끊는 식별자 (없으면 조용히 실패한다)
	UE_DEFINE_GAMEPLAY_TAG(Ability_Defense_Guard, "Ability.Defense.Guard");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Defense_Guard_Shield, "Ability.Defense.Guard.Shield");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Medical_Purify, "Ability.Medical.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Medical_Diagnose, "Ability.Medical.Diagnose");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Medical_Execute, "Ability.Medical.Execute");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Locomotion_Crouch, "Ability.Locomotion.Crouch");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Locomotion_Sprint, "Ability.Locomotion.Sprint");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Interact, "Ability.Interact");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Item_Use, "Ability.Item.Use");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Reaction, "Ability.Reaction");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Reaction_HitReact, "Ability.Reaction.HitReact");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Reaction_Stagger, "Ability.Reaction.Stagger");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Reaction_Knockdown, "Ability.Reaction.Knockdown");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Recovery_GetUp, "Ability.Recovery.GetUp");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Recovery_Ragdoll, "Ability.Recovery.Ragdoll");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Combat_FinisherWindow, "Ability.Combat.FinisherWindow");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Weapon_Summon, "Ability.Weapon.Summon");
	
	// State
	UE_DEFINE_GAMEPLAY_TAG(State_Dead, "State.Dead");
	UE_DEFINE_GAMEPLAY_TAG(State_Locomotion_Crouching, "State.Locomotion.Crouching");
	UE_DEFINE_GAMEPLAY_TAG(State_Locomotion_Sprinting, "State.Locomotion.Sprinting");
	UE_DEFINE_GAMEPLAY_TAG(State_Traversal_Hang, "State.Traversal.Hang");
	UE_DEFINE_GAMEPLAY_TAG(State_Traversal_Ladder, "State.Traversal.Ladder");
	UE_DEFINE_GAMEPLAY_TAG(State_Traversal_Climb, "State.Traversal.Climb");
	UE_DEFINE_GAMEPLAY_TAG(State_Traversal_Move, "State.Traversal.Move");
	UE_DEFINE_GAMEPLAY_TAG(State_Traversal_Exit, "State.Traversal.Exit");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_InCombat, "State.Combat.InCombat");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Attacking, "State.Combat.Attacking");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Attacking_Light, "State.Combat.Attacking.Light");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_AirOpening, "State.Combat.AirOpening");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_AirRecovery, "State.Combat.AirRecovery");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Attacking_Air, "State.Combat.Attacking.Air");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_RiposteWindow, "State.Combat.RiposteWindow");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Dodging, "State.Combat.Dodging");
	// 회피 커밋 - 무적(Dodging)과 별개로 공격 입력이 롤을 끊을 수 없는 구간
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_DodgeCommit, "State.Combat.DodgeCommit");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Staggered, "State.Combat.Staggered");
	// 그로기(다운) 중. UVBGA_Knockdown 이 넘어짐~기상 사이에 든다.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Downed, "State.Combat.Downed");
	// 가드/패링 상태 - VBGA_Guard 가 켜져 있는 동안 붙고, 데미지 hub 가 읽어 블록/패링으로 나눈다
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Guarding, "State.Combat.Guarding");
	// 슈퍼아머(SuperArmor): 보유 중엔 피격 히트리액트 몽타주를 스킵 -> 공격이 안 끊김. 거대검 강스윙 중 부여.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_SuperArmor, "State.Combat.SuperArmor");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_SuperArmor_Breakable, "State.Combat.SuperArmor.Breakable");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_PhaseTransition, "State.Combat.PhaseTransition");
	UE_DEFINE_GAMEPLAY_TAG(State_Encounter_Held, "State.Encounter.Held");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_FinisherWindow, "State.Combat.FinisherWindow");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_FinisherWindowArmed, "State.Combat.FinisherWindowArmed");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_PostureExecutable, "State.Combat.PostureExecutable");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_FinisherWindowImmune, "State.Combat.FinisherWindowImmune");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Finishing, "State.Combat.Finishing");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_CinematicSkill, "State.Combat.CinematicSkill");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_GettingUp, "State.Combat.GettingUp");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Ragdoll, "State.Combat.Ragdoll");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_LockedOn, "State.Combat.LockedOn");
	// 예고(텔레그래프) 중 - UVBGA_EnemyMeleeAttack 이 스윙 직전까지 로스태그로 부여한다.
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Telegraphing, "State.Combat.Telegraphing");
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_HitReacting, "State.Combat.HitReacting");
	UE_DEFINE_GAMEPLAY_TAG(State_Medical_Infected, "State.Medical.Infected");
	UE_DEFINE_GAMEPLAY_TAG(State_Medical_Purifying, "State.Medical.Purifying");
	
	UE_DEFINE_GAMEPLAY_TAG(State_Armed, "State.Armed");
	UE_DEFINE_GAMEPLAY_TAG(State_Armed_Blade, "State.Armed.Blade");
	UE_DEFINE_GAMEPLAY_TAG(State_Armed_BigSword, "State.Armed.BigSword");
	UE_DEFINE_GAMEPLAY_TAG(State_Armed_Fighter, "State.Armed.Fighter");
	UE_DEFINE_GAMEPLAY_TAG(State_Armed_Magic, "State.Armed.Magic");
	UE_DEFINE_GAMEPLAY_TAG(State_Armed_OneHandSword, "State.Armed.OneHandSword");
	
	UE_DEFINE_GAMEPLAY_TAG(State_Combat_Exiting, "State.Combat.Exiting");
	
	UE_DEFINE_GAMEPLAY_TAG(State_Weapon_Summoning, "State.Weapon.Summoning");
	UE_DEFINE_GAMEPLAY_TAG(State_Weapon_Sheathing, "State.Weapon.Sheathing");
	UE_DEFINE_GAMEPLAY_TAG(State_Weapon_TempSheathed, "State.Weapon.TempSheathed");
	UE_DEFINE_GAMEPLAY_TAG(State_Interacting, "State.Interacting");
	UE_DEFINE_GAMEPLAY_TAG(State_Interacting_SmartObject, "State.Interacting.SmartObject");

	// Effect
	UE_DEFINE_GAMEPLAY_TAG(Effect_Damage_Physical, "Effect.Damage.Physical");
	UE_DEFINE_GAMEPLAY_TAG(Effect_Damage_Infection, "Effect.Damage.Infection");
	UE_DEFINE_GAMEPLAY_TAG(Effect_Purify, "Effect.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Effect_Execute, "Effect.Execute");

	// Event
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_Hit, "Event.Combat.Hit");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_Death, "Event.Combat.Death");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_TelegraphStart, "Event.Combat.TelegraphStart");
	// Poise 파괴 에지 - UVBCombatAttributeSet 이 쏘고 처형 창 또는 UVBGA_Stagger 가 받는다.
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_PoiseBroken, "Event.Combat.PoiseBroken");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_ExecuteAvailable, "Event.Combat.ExecuteAvailable");
	// 넘어뜨림 이상의 실효 등급 - 허브가 Hit 보다 먼저 쏘고 UVBGA_Knockdown 이 받는다.
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_KnockedDown, "Event.Combat.KnockedDown");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_LaunchedTarget, "Event.Combat.LaunchedTarget");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_AirSlam, "Event.Combat.AirSlam");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_AirHold, "Event.Combat.AirHold");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_AirRelease, "Event.Combat.AirRelease");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_AirSlam, "GameplayCue.Combat.AirSlam");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_FinisherSurvived, "Event.Combat.FinisherSurvived");
	// 체력 문턱 창 요청 - 창 설정이 HealthThreshold 인 적에게만 VBDamageResponse 가 쏜다.
	UE_DEFINE_GAMEPLAY_TAG(Event_Combat_FinisherWindow, "Event.Combat.FinisherWindow");
	UE_DEFINE_GAMEPLAY_TAG(Event_Medical_Cured, "Event.Medical.Cured");
	UE_DEFINE_GAMEPLAY_TAG(Event_Montage_AttackTrace, "Event.Montage.AttackTrace");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_WindowOpen, "Event.Combo.WindowOpen");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_WindowClose, "Event.Combo.WindowClose");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_InputReceived, "Event.Combo.InputReceived");
	// 입력 분배가 준비한 이벤트 데이터로 능력을 켤 때의 이벤트 태그(UVBGameplayAbility::PrepareActivation). 태그가 없으면 엔진이 클라에 데이터를 전달하지 않는다.
	UE_DEFINE_GAMEPLAY_TAG(Event_Ability_InputActivate, "Event.Ability.InputActivate");

	// Input
	UE_DEFINE_GAMEPLAY_TAG(Input_Attack_Light, "Input.Attack.Light");
	UE_DEFINE_GAMEPLAY_TAG(Input_Attack_Armored, "Input.Attack.Armored");
	UE_DEFINE_GAMEPLAY_TAG(Input_Attack_Boss_Phase2, "Input.Attack.Boss.Phase2");
	UE_DEFINE_GAMEPLAY_TAG(Input_Attack_Boss_Down, "Input.Attack.Boss.Down");
	// 가드 입력 - RMB 홀드
	UE_DEFINE_GAMEPLAY_TAG(Input_Combat_Guard, "Input.Combat.Guard");
	UE_DEFINE_GAMEPLAY_TAG(Input_CinematicSkill, "Input.CinematicSkill");
	UE_DEFINE_GAMEPLAY_TAG(Input_Dodge, "Input.Dodge");
	UE_DEFINE_GAMEPLAY_TAG(Input_Purify, "Input.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Input_Execute, "Input.Execute");
	UE_DEFINE_GAMEPLAY_TAG(Input_Crouch, "Input.Crouch");
	UE_DEFINE_GAMEPLAY_TAG(Input_Sprint, "Input.Sprint");
	UE_DEFINE_GAMEPLAY_TAG(Input_Aim, "Input.Aim");
	UE_DEFINE_GAMEPLAY_TAG(Input_LockOn, "Input.LockOn");
	UE_DEFINE_GAMEPLAY_TAG(Input_SwitchTarget, "Input.SwitchTarget");
	UE_DEFINE_GAMEPLAY_TAG(Input_WeaponSummon, "Input.WeaponSummon");
	UE_DEFINE_GAMEPLAY_TAG(Input_Interact, "Input.Interact");
	UE_DEFINE_GAMEPLAY_TAG(Input_Move, "Input.Move");
	UE_DEFINE_GAMEPLAY_TAG(Input_Look, "Input.Look");
	UE_DEFINE_GAMEPLAY_TAG(Input_Jump, "Input.Jump");
	UE_DEFINE_GAMEPLAY_TAG(Input_WalkToggle, "Input.WalkToggle");
	UE_DEFINE_GAMEPLAY_TAG(Input_WeaponSlot_1, "Input.WeaponSlot.1");
	UE_DEFINE_GAMEPLAY_TAG(Input_WeaponSlot_2, "Input.WeaponSlot.2");
	UE_DEFINE_GAMEPLAY_TAG(Input_WeaponSlot_3, "Input.WeaponSlot.3");
	UE_DEFINE_GAMEPLAY_TAG(Input_Ragdoll, "Input.Ragdoll");

	// AI
	UE_DEFINE_GAMEPLAY_TAG(AI_State_Idle, "AI.State.Idle");
	UE_DEFINE_GAMEPLAY_TAG(AI_State_Combat, "AI.State.Combat");
	UE_DEFINE_GAMEPLAY_TAG(AI_State_Dead, "AI.State.Dead");
	
	// DATA (SetByCaller용)
	UE_DEFINE_GAMEPLAY_TAG(Data_ReputationChange, "Data.ReputationChange");
	UE_DEFINE_GAMEPLAY_TAG(Data_MedicalAmount, "Data.MedicalAmount");
	UE_DEFINE_GAMEPLAY_TAG(Data_KnockbackStrength, "Data.KnockbackStrength");
	UE_DEFINE_GAMEPLAY_TAG(Data_KnockbackZ, "Data.KnockbackZ");
	UE_DEFINE_GAMEPLAY_TAG(Data_PoiseDamage, "Data.PoiseDamage");

	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_None, "Combat.Impact.None");
	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_Light, "Combat.Impact.Light");
	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_Heavy, "Combat.Impact.Heavy");
	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_Knockdown, "Combat.Impact.Knockdown");
	UE_DEFINE_GAMEPLAY_TAG(Combat_Impact_Launch, "Combat.Impact.Launch");

	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Purify, "Cooldown.Purify");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Execute, "Cooldown.Execute");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Attack_Light, "Cooldown.Attack.Light");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Attack_Armored, "Cooldown.Attack.Armored");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Dodge, "Cooldown.Dodge");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_FinisherWindow, "Cooldown.FinisherWindow");
	
	// GameplayCue
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Damage_Physical, "GameplayCue.Damage.Physical");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Damage_Physical_Heavy, "GameplayCue.Damage.Physical.Heavy");
	// 주먹, 발 타격 - 칼(Physical)과 다른 둔탁한 이펙트. 모양 표(FVBMeleeTraceShapeDef::ImpactCueTag)가 고른다.
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Damage_Blunt, "GameplayCue.Damage.Blunt");
	// 주먹, 발 강타(마무리타, 띄우기). 스윙의 강타 등급이 Blunt 를 이 자식으로 바꾼다(UVBSwingCombatProfile::ResolveImpactCue).
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Damage_Blunt_Heavy, "GameplayCue.Damage.Blunt.Heavy");
	// 출혈 폭발 전용(GE_Bleed_Burst) - 강타 큐를 같이 쓰면 강타 이펙트를 바꿀 때 출혈도 바뀐다.
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Damage_Bleed_Burst, "GameplayCue.Damage.Bleed.Burst");
	// 처형 타격 전용 - 평타와 같은 큐를 쓰면 처형 연출만 따로 못 고친다. 처형 설정(UVBFinisherConfig::StrikeCueTag)이 고른다.
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Finisher_Strike, "GameplayCue.Finisher.Strike");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_State_Staggered, "GameplayCue.State.Staggered");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_State_Death, "GameplayCue.State.Death");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_HitStop, "GameplayCue.Combat.HitStop");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_HitFlash, "GameplayCue.Combat.HitFlash");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_HitPhysics, "GameplayCue.Combat.HitPhysics");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_FinisherWindow, "GameplayCue.Combat.FinisherWindow");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_SwingImpact, "GameplayCue.Combat.SwingImpact");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_Telegraph, "GameplayCue.Combat.Telegraph");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_Parry, "GameplayCue.Combat.Parry");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_Combat_GuardBlock, "GameplayCue.Combat.GuardBlock");

	// Attack.Answer - 공격에 대한 답(패링 가능, 회피만). 예고 큐(약속)와 데미지 허브(이행)가 같은 값을 읽는다.
	UE_DEFINE_GAMEPLAY_TAG(Attack_Answer_Parryable, "Attack.Answer.Parryable");
	UE_DEFINE_GAMEPLAY_TAG(Attack_Answer_DodgeOnly, "Attack.Answer.DodgeOnly");

	// UI
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_WeaponWheel, "UI.Slot.HUD.WeaponWheel");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_QuestTracker, "UI.Slot.HUD.QuestTracker");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_Progression, "UI.Slot.HUD.Progression");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_QuickSlots, "UI.Slot.HUD.QuickSlots");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_ChapterCard, "UI.Slot.HUD.ChapterCard");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_RegionBanner, "UI.Slot.HUD.RegionBanner");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_TutorialHint, "UI.Slot.HUD.TutorialHint");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_WorldIndicators, "UI.Slot.HUD.WorldIndicators");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_Subtitle, "UI.Slot.HUD.Subtitle");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_TouchControls, "UI.Slot.HUD.TouchControls");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_BossBar, "UI.Slot.HUD.BossBar");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_Minimap, "UI.Slot.HUD.Minimap");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_CinematicSkill, "UI.Slot.HUD.CinematicSkill");
	UE_DEFINE_GAMEPLAY_TAG(UI_Slot_HUD_Stamina, "UI.Slot.HUD.Stamina");
	UE_DEFINE_GAMEPLAY_TAG(Indicator_Group_Execute, "Indicator.Group.Execute");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_HUD, "UI.Screen.HUD");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Pause, "UI.Screen.Pause");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_GameOver, "UI.Screen.GameOver");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_MainMenu, "UI.Screen.MainMenu");

	// 무기 궤적 판정
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape, "Melee.Shape");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_Weapon, "Melee.Shape.Weapon");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_HandL, "Melee.Shape.HandL");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_HandR, "Melee.Shape.HandR");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_FootL, "Melee.Shape.FootL");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_FootR, "Melee.Shape.FootR");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Shape_Slam, "Melee.Shape.Slam");
	UE_DEFINE_GAMEPLAY_TAG(Melee_Window, "Melee.Window");
	UE_DEFINE_GAMEPLAY_TAG(Event_Montage_AttackTraceEnd, "Event.Montage.AttackTraceEnd");
	UE_DEFINE_GAMEPLAY_TAG(Event_Montage_ProjectileRelease, "Event.Montage.ProjectileRelease");

	// 아이템
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Acquired, "Event.Item.Acquired");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Used, "Event.Item.Used");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Use, "Event.Item.Use");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_UseMarker, "Event.Item.UseMarker");
	UE_DEFINE_GAMEPLAY_TAG(Equipment_Slot_Head, "Equipment.Slot.Head");
	UE_DEFINE_GAMEPLAY_TAG(Equipment_Slot_Body, "Equipment.Slot.Body");
	UE_DEFINE_GAMEPLAY_TAG(Equipment_Slot_Arms, "Equipment.Slot.Arms");
	UE_DEFINE_GAMEPLAY_TAG(Equipment_Slot_Legs, "Equipment.Slot.Legs");
	UE_DEFINE_GAMEPLAY_TAG(Equipment_Slot_Charm, "Equipment.Slot.Charm");

	UE_DEFINE_GAMEPLAY_TAG(Appearance_Slot, "Appearance.Slot");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Slot_Head, "Appearance.Slot.Head");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Slot_Top, "Appearance.Slot.Top");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Slot_Bottom, "Appearance.Slot.Bottom");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Slot_Hands, "Appearance.Slot.Hands");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Section, "Appearance.Section");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Section_Head, "Appearance.Section.Head");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Section_Hair, "Appearance.Section.Hair");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Section_Hands, "Appearance.Section.Hands");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Section_TorsoUpperArms, "Appearance.Section.TorsoUpperArms");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Section_Forearms, "Appearance.Section.Forearms");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Section_HipsLegs, "Appearance.Section.HipsLegs");
	UE_DEFINE_GAMEPLAY_TAG(Appearance_Section_Feet, "Appearance.Section.Feet");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Equipped, "Event.Item.Equipped");
	UE_DEFINE_GAMEPLAY_TAG(Event_Item_Unequipped, "Event.Item.Unequipped");
	UE_DEFINE_GAMEPLAY_TAG(Event_Progression_LevelUp, "Event.Progression.LevelUp");
	UE_DEFINE_GAMEPLAY_TAG(Event_Progression_SkillUnlocked, "Event.Progression.SkillUnlocked");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_SkillTree, "UI.Screen.SkillTree");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Inventory, "UI.Screen.Inventory");
	UE_DEFINE_GAMEPLAY_TAG(Data_Heal, "Data.Heal");
	UE_DEFINE_GAMEPLAY_TAG(Data_WeaponDamageScale, "Data.WeaponDamageScale");
	UE_DEFINE_GAMEPLAY_TAG(Event_Dialogue_NodeReached, "Event.Dialogue.NodeReached");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Dialogue, "UI.Screen.Dialogue");
	UE_DEFINE_GAMEPLAY_TAG(Event_Enemy_Killed, "Event.Enemy.Killed");
	UE_DEFINE_GAMEPLAY_TAG(Message_Death, "Message.Death");
	UE_DEFINE_GAMEPLAY_TAG(Message_Combat_HitReceived, "Message.Combat.HitReceived");
	UE_DEFINE_GAMEPLAY_TAG(Message_Combat_ChargeCredit, "Message.Combat.ChargeCredit");
	UE_DEFINE_GAMEPLAY_TAG(Data_CinematicSkillGaugeGain, "Data.CinematicSkillGaugeGain");
	UE_DEFINE_GAMEPLAY_TAG(GameplayCue_CinematicSkill_Ready, "GameplayCue.CinematicSkill.Ready");
	UE_DEFINE_GAMEPLAY_TAG(Event_Interaction_Completed, "Event.Interaction.Completed");
	UE_DEFINE_GAMEPLAY_TAG(Event_Area_Entered, "Event.Area.Entered");
	UE_DEFINE_GAMEPLAY_TAG(Event_Quest_Started, "Event.Quest.Started");
	UE_DEFINE_GAMEPLAY_TAG(Event_Quest_StageAdvanced, "Event.Quest.StageAdvanced");
	UE_DEFINE_GAMEPLAY_TAG(Event_Quest_Completed, "Event.Quest.Completed");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Journal, "UI.Screen.Journal");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Shop, "UI.Screen.Shop");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Crafting, "UI.Screen.Crafting");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_FastTravel, "UI.Screen.FastTravel");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_KeyBindings, "UI.Screen.KeyBindings");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Options, "UI.Screen.Options");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Collection, "UI.Screen.Collection");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Tutorial, "UI.Screen.Tutorial");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_DifficultyPicker, "UI.Screen.DifficultyPicker");
	UE_DEFINE_GAMEPLAY_TAG(UI_Screen_Confirmation, "UI.Screen.Confirmation");

	UE_DEFINE_GAMEPLAY_TAG(Tutorial, "Tutorial");

	// 능력 발동 실패 이유
	UE_DEFINE_GAMEPLAY_TAG(Ability_Fail_Cooldown, "Ability.Fail.Cooldown");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Fail_Cost, "Ability.Fail.Cost");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Fail_Networking, "Ability.Fail.Networking");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Fail_TagsBlocked, "Ability.Fail.TagsBlocked");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Fail_TagsMissing, "Ability.Fail.TagsMissing");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Fail_NoMedicalTarget, "Ability.Fail.NoMedicalTarget");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Fail_NoOwnedWeapon, "Ability.Fail.NoOwnedWeapon");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Fail_Unknown, "Ability.Fail.Unknown");

	UE_DEFINE_GAMEPLAY_TAG(Passive, "Passive");
	UE_DEFINE_GAMEPLAY_TAG(Passive_Dodge, "Passive.Dodge");
	UE_DEFINE_GAMEPLAY_TAG(Passive_Parry, "Passive.Parry");
	UE_DEFINE_GAMEPLAY_TAG(Passive_Riposte, "Passive.Riposte");
	UE_DEFINE_GAMEPLAY_TAG(Passive_Execute, "Passive.Execute");
}
