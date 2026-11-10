// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "PlayBot/VBPlayBotHospitalOpening.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBPushableComponent.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Traversal/VBTraversalComponent.h"

bool FVBPlayBotOpeningStep::Parse(const TArray<FString>& Rows, TArray<FVBPlayBotOpeningStep>& Out, FString& Error)
{
	TArray<FVBPlayBotOpeningStep> Parsed;
	TSet<FName> Ids;
	for (const FString& Row : Rows)
	{
		TArray<FString> Fields; Row.ParseIntoArray(Fields, TEXT("|"), false);
		if (Fields.Num() != 4 && Fields.Num() != 5 && Fields.Num() != 6) { Error = TEXT("native step field count must match its declared action"); return false; }
		FVBPlayBotOpeningStep Step;
		Step.Id = FName(*Fields[0]);
		if (Step.Id.IsNone() || Ids.Contains(Step.Id)) { Error = TEXT("native step identity is missing or duplicate"); return false; }
		Ids.Add(Step.Id);
		if (!LexTryParseString(Step.Timeout, *Fields.Last()) || !FMath::IsFinite(Step.Timeout) || Step.Timeout <= 0 || Step.Timeout > 120)
		{ Error = TEXT("native step timeout is invalid"); return false; }
		if ((Fields[1] == TEXT("Interact") || Fields[1] == TEXT("InteractActor")) && Fields.Num() == 6)
		{
			Step.Kind = EVBPlayBotOpeningAction::Interact;
			Step.ClassPath = Fields[3];
			if (Fields[1] == TEXT("InteractActor"))
			{
				Step.TargetPath = Fields[2];
				if (!Step.TargetPath.StartsWith(TEXT("/Game/")) || !Step.TargetPath.Contains(TEXT(":PersistentLevel.")))
				{ Error = TEXT("native actor interaction needs an exact level actor object path"); return false; }
			}
			else
			{
				Step.SaveKey = FName(*Fields[2]);
				if (Step.SaveKey.IsNone()) { Error = TEXT("native saved interaction needs a save identity"); return false; }
			}
			if (Step.ClassPath.IsEmpty() || (Fields[4] != TEXT("Activated") && Fields[4] != TEXT("Deactivated") && Fields[4] != TEXT("WeaponAcquired")))
			{ Error = TEXT("native interaction class/evidence is invalid"); return false; }
			Step.bTargetActivated = Fields[4] != TEXT("Deactivated");
			Step.bWeaponAcquisition = Fields[4] == TEXT("WeaponAcquired");
		}
		else if ((Fields[1] == TEXT("Push") || Fields[1] == TEXT("PushActor")) && Fields.Num() == 6)
		{
			// Id|Push|SaveKey 또는 Id|PushActor|레벨 액터 경로, 그다음 ActorClass|최소 이동 cm|제한 시간.
			// 잡기, 앞으로 밀기, 놓기를 모두 실제 입력으로 한다. 저장 식별자가 없는 밀기 대상은 액터 경로로 고른다.
			Step.Kind = EVBPlayBotOpeningAction::Push;
			Step.ClassPath = Fields[3];
			if (Fields[1] == TEXT("PushActor"))
			{
				Step.TargetPath = Fields[2];
				if (!Step.TargetPath.StartsWith(TEXT("/Game/")) || !Step.TargetPath.Contains(TEXT(":PersistentLevel.")))
				{ Error = TEXT("native actor push needs an exact level actor object path"); return false; }
			}
			else { Step.SaveKey = FName(*Fields[2]); }
			if ((Step.TargetPath.IsEmpty() && Step.SaveKey.IsNone()) || Step.ClassPath.IsEmpty()) { Error = TEXT("native push needs a target identity and actor class"); return false; }
			if (!LexTryParseString(Step.PushMinDistance, *Fields[4]) || !FMath::IsFinite(Step.PushMinDistance)
				|| Step.PushMinDistance <= 0.0 || Step.PushMinDistance > 1000.0)
			{ Error = TEXT("native push distance must be in (0,1000] cm"); return false; }
		}
		else if ((Fields[1] == TEXT("Walk") || Fields[1] == TEXT("Jump") || Fields[1] == TEXT("Traverse") || Fields[1] == TEXT("Ladder"))
			&& Fields.Num() == 4)
		{
			Step.Kind = Fields[1] == TEXT("Walk") ? EVBPlayBotOpeningAction::Walk : EVBPlayBotOpeningAction::Jump;
			Step.JumpEvidence = Fields[1] == TEXT("Traverse") ? EVBPlayBotJumpEvidence::TraversalMove
				: Fields[1] == TEXT("Ladder") ? EVBPlayBotJumpEvidence::Ladder : EVBPlayBotJumpEvidence::Airborne;
			TArray<FString> XYZ; Fields[2].ParseIntoArray(XYZ, TEXT(","), false);
			if (XYZ.Num() != 3 || !LexTryParseString(Step.Point.X, *XYZ[0]) || !LexTryParseString(Step.Point.Y, *XYZ[1])
				|| !LexTryParseString(Step.Point.Z, *XYZ[2]) || Step.Point.ContainsNaN())
			{ Error = TEXT("native walk/jump point is invalid"); return false; }
		}
		else if (Fields[1] == TEXT("Draw") && Fields.Num() == 5 && Fields[2] == TEXT("OneHandSword"))
		{
			const FGameplayTag DrawTag = FGameplayTag::RequestGameplayTag(FName(*Fields[3]),false);
			if (!DrawTag.IsValid() || !Fields[3].StartsWith(TEXT("Input.")))
			{ Error = TEXT("native draw needs an explicit registered Input action"); return false; }
			Step.Kind = EVBPlayBotOpeningAction::Draw; Step.DrawAction = DrawTag.GetTagName();
		}
		else if (Fields[1] == TEXT("Fight") && Fields.Num() == 4 && !Fields[2].IsEmpty())
		{
			Step.Kind = EVBPlayBotOpeningAction::Fight; Step.FightGroup = FName(*Fields[2]);
			if (Step.FightGroup.IsNone()) { Error = TEXT("native fight group is empty"); return false; }
		}
		else if (Fields[1] == TEXT("Observe") && Fields.Num() == 4)
		{
			Step.Kind = EVBPlayBotOpeningAction::Observe;
			Step.ObserveFact = FGameplayTag::RequestGameplayTag(FName(*Fields[2]),false);
			if (!Step.ObserveFact.IsValid()) { Error = TEXT("native observed fact is not registered"); return false; }
		}
		else { Error = TEXT("unknown native step action or field count"); return false; }
		Parsed.Add(MoveTemp(Step));
	}
	if (Parsed.IsEmpty()) { Error = TEXT("native route has no declared steps"); return false; }
	Out = MoveTemp(Parsed);
	return true;
}

bool FVBPlayBotHospitalObservation::Bind(UWorld* World, const FVBPlayBotOpeningStep& Step, FString& Error)
{
	Controller = World ? World->GetFirstPlayerController() : nullptr;
	Player = Controller.IsValid() ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
	ReadyFact = FGameplayTag::RequestGameplayTag(TEXT("Story.Prologue.Woke"), false);
	WeaponFact = FGameplayTag::RequestGameplayTag(TEXT("Story.Prologue.WeaponFound"), false);
	bPushTarget = Step.Kind == EVBPlayBotOpeningAction::Push;
	bNeedsTarget = Step.Kind == EVBPlayBotOpeningAction::Interact || bPushTarget;
	bNeedsWeapon = Step.bWeaponAcquisition;
	bTargetActivated = Step.bTargetActivated;
	ObserveFact = Step.ObserveFact;
	Target.Reset();
	if (!Player.IsValid() || !ReadyFact.IsValid() || !WeaponFact.IsValid())
	{ Error = TEXT("opening needs the actual character and declared story facts"); return false; }
	if (!bNeedsTarget) { return true; }
	if (Step.TargetPath.IsEmpty() == Step.SaveKey.IsNone())
	{ Error = TEXT("opening target needs exactly one actor path or save identity"); return false; }
	UClass* ExpectedClass = LoadObject<UClass>(nullptr, *Step.ClassPath);
	if (!ExpectedClass) { Error = TEXT("opening target class is unavailable"); return false; }
	int32 Matches = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!Step.TargetPath.IsEmpty() && It->GetPathName() != Step.TargetPath) { continue; }
		TArray<UVBInteractableComponent*> Components; It->GetComponents(Components);
		for (UVBInteractableComponent* Component : Components)
		{
			if (!Step.TargetPath.IsEmpty() || Component->GetSaveKey() == Step.SaveKey)
			{
				++Matches;
				if (!It->IsA(ExpectedClass)) { Error = TEXT("opening target has the wrong class"); return false; }
				Target = Component;
			}
		}
	}
	if (Matches != 1) { Error = FString::Printf(TEXT("opening target count is %d, expected exactly one"), Matches); return false; }
	if (bPushTarget)
	{
		// 밀기 대상은 켜짐/꺼짐 결과가 없다. 증거는 잡힘과 실제 이동 거리이므로 시작 자리를 남긴다.
		if (!Cast<UVBPushableComponent>(Target.Get())) { Error = TEXT("push target interaction is not a pushable"); return false; }
		TargetStart = Target->GetOwner()->GetActorLocation();
		return true;
	}
	if (Target->IsActivated() == bTargetActivated) { Error = TEXT("opening target is already in its declared result state before input"); return false; }
	if (bNeedsWeapon)
	{
		FVBPlayBotOpeningView Before;
		if (!Read(World, Before, Error)) { return false; }
		if (Before.bWeaponOwned || Before.bWeaponFact)
		{ Error = TEXT("weapon acquisition evidence already exists before pickup input"); return false; }
	}
	return true;
}

bool FVBPlayBotHospitalObservation::Read(UWorld* World, FVBPlayBotOpeningView& Out, FString& Error) const
{
	if (!World || !Player.IsValid() || !Controller.IsValid() || (bNeedsTarget && !Target.IsValid())
		|| Player->GetWorld() != World || Controller->GetPawn() != Player.Get())
	{ Error = TEXT("opening world, player, controller or target changed"); return false; }
	const UVBStoryStateComponent* Story = UVBStoryStateComponent::Get(World);
	const UVBInteractorComponent* Interactor = UVBInteractorComponent::FindFor(Player.Get());
	const UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Player.Get());
	if (!Story || !Interactor || !Weapons) { Error = TEXT("opening story, interaction or weapon owner is absent"); return false; }
	Out.Position = Player->GetActorLocation();
	Out.Feet = Out.Position - FVector(0, 0, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	Out.Velocity = Player->GetVelocity();
	FRotator CameraRotation; Controller->GetPlayerViewPoint(Out.Camera, CameraRotation);
	Out.CameraYaw = CameraRotation.Yaw;
	Out.MoveYaw = Controller->GetControlRotation().Yaw;
	Out.FacingYaw = Player->GetActorRotation().Yaw;
	Out.bReady = Story->HasFact(ReadyFact);
	Out.bWeaponFact = Story->HasFact(WeaponFact);
	Out.bWeaponOwned = Weapons->IsWeaponOwned(EVBWeaponType::OneHandSword);
	Out.bWeaponDrawn = Weapons->IsWeaponSummoned() && Weapons->GetCurrentWeaponType() == EVBWeaponType::OneHandSword;
	Out.bObservedFact = ObserveFact.IsValid() && Story->HasFact(ObserveFact);
	if (bNeedsTarget)
	{
		Out.Target = Target->GetComponentLocation();
		Out.bFocused = Interactor->GetFocusedInteractable() == Target.Get();
		Out.bActivated = Target->IsActivated();
		Out.bTargetComplete = (Out.bActivated == bTargetActivated) && (!bNeedsWeapon || (Out.bWeaponFact && Out.bWeaponOwned && Target->GetOwner()->IsHidden()));
	}
	if (bPushTarget)
	{
		Out.bHolding = Interactor->GetPushable() == Target.Get();
		Out.TargetDisplacement = FVector::Dist2D(Target->GetOwner()->GetActorLocation(), TargetStart);
	}
	Out.bGrounded = Player->GetCharacterMovement()->IsMovingOnGround();
	Out.MovementMode = static_cast<uint8>(Player->GetCharacterMovement()->MovementMode);
	// 트래버설 컴포넌트가 없는 캐릭터면 넘기, 사다리 증거는 거짓으로 남아 해당 단계가 통과하지 못한다.
	if (const UVBTraversalComponent* Traversal = UVBTraversalComponent::FindFor(Player.Get()))
	{
		Out.bTraversalMove = Traversal->GetTraversalPhase() == EVBTraversalPhase::Move;
		Out.bOnLadder = Traversal->IsOnLadder();
	}
	return true;
}

bool FVBPlayBotHospitalActions::Schedule(FName InputAction, double Seconds, FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human)
{
	FVBPlayBotPress Press;
	Press.Action = InputAction;
	Press.Cue = Step.Id.ToString();
	Press.DelayMs = Human.SampleSimpleReactionMs();
	Press.DueSeconds = Seconds + Press.DelayMs / 1000.0;
	Press.HoldSeconds = Human.SampleHoldMs() / 1000.0;
	PressId = Input.Schedule(Press);
	ActSeconds = Seconds;
	return PressId != INDEX_NONE;
}

EVBPlayBotOpeningResult FVBPlayBotHospitalActions::TickMovement(const FVBPlayBotOpeningView& View, double Seconds,
	FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human, FString& Error)
{
	if (!View.bReady) { Input.SetStickWant(FVector2D::ZeroVector); Input.SetLookWant(FVector2D::ZeroVector); return EVBPlayBotOpeningResult::Running; }
	const bool bJump = Step.Kind == EVBPlayBotOpeningAction::Jump;
	const bool bNeedsTraversal = bJump && Step.JumpEvidence != EVBPlayBotJumpEvidence::Airborne;
	if (bInjected && !View.bGrounded) { bSawAirborne = true; }
	if (bInjected && ((Step.JumpEvidence == EVBPlayBotJumpEvidence::TraversalMove && View.bTraversalMove)
		|| (Step.JumpEvidence == EVBPlayBotJumpEvidence::Ladder && View.bOnLadder)))
	{ bSawJumpEvidence = true; }
	// 점프 입력 뒤 떴다가 다시 섰는데 넘기/사다리가 한 번도 안 보였으면, 게임이 넘기 대신 그냥 점프를 했다.
	// 시간 초과까지 기다리지 않고 그 사실을 바로 실패 이유로 남긴다.
	if (bNeedsTraversal && bInjected && bSawAirborne && View.bGrounded && !bSawJumpEvidence)
	{
		Error = Step.JumpEvidence == EVBPlayBotJumpEvidence::Ladder
			? TEXT("jump input did not attach to the ladder - the character jumped and landed")
			: TEXT("jump input did not start a traversal (vault/mantle) - the character jumped and landed");
		return EVBPlayBotOpeningResult::Failed;
	}
	const FVector Delta = Step.Point - View.Feet;
	const bool bArrived = Delta.Size2D() <= 25.0 && FMath::Abs(Delta.Z) <= 35.0 && View.bGrounded;
	const bool bTraversalObserved = !bJump || (bInjected && (bNeedsTraversal ? bSawJumpEvidence : bSawAirborne));
	if (bArrived && bTraversalObserved)
	{
		Input.SetStickWant(FVector2D::ZeroVector); Input.SetLookWant(FVector2D::ZeroVector);
		if (CompleteSeconds < 0.0) { CompleteSeconds = Seconds; }
		return Seconds - CompleteSeconds >= 0.3 ? EVBPlayBotOpeningResult::PartialComplete : EVBPlayBotOpeningResult::Running;
	}
	CompleteSeconds = -1.0;
	const double Magnitude = Human.GetProfile().MoveMagnitude * FMath::Clamp(Delta.Size2D()/160.0,0.25,1.0);
	Steering.Drive({Delta,View.MoveYaw,View.CameraYaw},Magnitude,Seconds,Input,Human);
	const double WantYaw = Delta.Rotation().Yaw;
	if (Step.Kind == EVBPlayBotOpeningAction::Jump && PressId == INDEX_NONE && View.bGrounded
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(View.FacingYaw,WantYaw)) <= 20.0)
	{
		if (!Schedule(TEXT("Input.Jump"),Seconds,Input,Human)) { Error = TEXT("native jump mapping/schedule failed"); return EVBPlayBotOpeningResult::Failed; }
	}
	return EVBPlayBotOpeningResult::Running;
}

EVBPlayBotOpeningResult FVBPlayBotHospitalActions::Tick(const FVBPlayBotOpeningView& View, double Seconds,
	FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human, FString& Error)
{
	if (Step.Kind == EVBPlayBotOpeningAction::Observe)
	{
		Input.SetStickWant(FVector2D::ZeroVector); Input.SetLookWant(FVector2D::ZeroVector);
		return View.bReady && View.bObservedFact ? EVBPlayBotOpeningResult::PartialComplete : EVBPlayBotOpeningResult::Running;
	}
	if (Step.Kind == EVBPlayBotOpeningAction::Draw)
	{
		Input.SetStickWant(FVector2D::ZeroVector); Input.SetLookWant(FVector2D::ZeroVector);
		if (View.bWeaponDrawn)
		{
			if (!bSawSheathed || !bInjected) { Error = TEXT("weapon was drawn without the observed declared draw input"); return EVBPlayBotOpeningResult::Failed; }
			if (CompleteSeconds < 0) { CompleteSeconds = Seconds; }
			return Seconds-CompleteSeconds >= 0.3 ? EVBPlayBotOpeningResult::PartialComplete : EVBPlayBotOpeningResult::Running;
		}
		CompleteSeconds = -1.0;
		bSawSheathed = true;
		if (!View.bReady || !View.bGrounded) { return EVBPlayBotOpeningResult::Running; }
		if (!View.bWeaponOwned) { Error = TEXT("draw requires actual OneHandSword ownership"); return EVBPlayBotOpeningResult::Failed; }
		if (PressId == INDEX_NONE && !Schedule(Step.DrawAction,Seconds,Input,Human))
		{ Error = TEXT("native draw mapping/schedule failed"); return EVBPlayBotOpeningResult::Failed; }
		if (Seconds-ActSeconds > EffectTimeout) { Error = TEXT("declared normal input did not draw OneHandSword"); return EVBPlayBotOpeningResult::Failed; }
		return EVBPlayBotOpeningResult::Running;
	}
	if (Step.Kind == EVBPlayBotOpeningAction::Walk || Step.Kind == EVBPlayBotOpeningAction::Jump) { return TickMovement(View,Seconds,Input,Human,Error); }
	if (Step.Kind == EVBPlayBotOpeningAction::Push) { return TickPush(View,Seconds,Input,Human,Error); }
	if (Step.Kind != EVBPlayBotOpeningAction::Interact) { Error = TEXT("opening action cannot run a fight policy"); return EVBPlayBotOpeningResult::Failed; }

	Input.SetStickWant(FVector2D::ZeroVector);
	if (View.bActivated != Step.bTargetActivated) { bSawOppositeState = true; }
	if (View.bTargetComplete)
	{
		Input.SetLookWant(FVector2D::ZeroVector);
		if (!bSawOppositeState || !bSawFocus || !bInjected)
		{ Error = TEXT("target completed without the observed opposite-state/focused input sequence"); return EVBPlayBotOpeningResult::Failed; }
		if (CompleteSeconds < 0.0) { CompleteSeconds = Seconds; }
		return Seconds-CompleteSeconds >= 1.0 ? EVBPlayBotOpeningResult::PartialComplete : EVBPlayBotOpeningResult::Running;
	}
	CompleteSeconds = -1.0;
	if (!View.bReady || !View.bGrounded) { Input.SetLookWant(FVector2D::ZeroVector); return EVBPlayBotOpeningResult::Running; }
	if (PressId != INDEX_NONE)
	{
		if (!bInjected && !View.bFocused) { Error = TEXT("interaction focus lost before scheduled input"); return EVBPlayBotOpeningResult::Failed; }
		Input.SetLookWant(FVector2D::ZeroVector);
		if (Seconds-ActSeconds > EffectTimeout) { Error = TEXT("interact input did not produce its declared result"); return EVBPlayBotOpeningResult::Failed; }
		return EVBPlayBotOpeningResult::Running;
	}
	if (View.bFocused)
	{
		bSawFocus = true; Input.SetLookWant(FVector2D::ZeroVector);
		if (!Schedule(Action,Seconds,Input,Human)) { Error = TEXT("native interaction schedule was rejected"); return EVBPlayBotOpeningResult::Failed; }
		return EVBPlayBotOpeningResult::Running;
	}
	LookAtTarget(View,Input);
	return EVBPlayBotOpeningResult::Running;
}

void FVBPlayBotHospitalActions::LookAtTarget(const FVBPlayBotOpeningView& View, FVBPlayBotInputDriver& Input) const
{
	// 오른쪽 스틱은 0.265 아래를 데드존으로 버린다(XInput 8689/32767). 작은 각도에서 0.15 처럼 약하게 밀면
	// 카메라가 전혀 돌지 않아 초점이 영영 안 잡혔으므로, 돌릴 때는 데드존 바깥 크기부터 쓴다.
	constexpr double MinLook = 0.3;
	const double Yaw = (View.Target-View.Camera).Rotation().Yaw;
	const double DeltaYaw = FMath::FindDeltaAngleDegrees(View.CameraYaw,Yaw);
	const double Look = FMath::Abs(DeltaYaw) < 2.0 ? 0.0 : FMath::Sign(DeltaYaw) * FMath::Clamp(FMath::Abs(DeltaYaw)/60.0,MinLook,0.5);
	Input.SetLookWant(FVector2D(Look,0.0));
}

EVBPlayBotOpeningResult FVBPlayBotHospitalActions::TickPush(const FVBPlayBotOpeningView& View, double Seconds,
	FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human, FString& Error)
{
	if (!View.bReady) { Input.SetStickWant(FVector2D::ZeroVector); Input.SetLookWant(FVector2D::ZeroVector); return EVBPlayBotOpeningResult::Running; }
	switch (PushPhase)
	{
	case EPushPhase::Grab:
		Input.SetStickWant(FVector2D::ZeroVector);
		if (View.bHolding)
		{
			if (!bInjected || !bSawFocus) { Error = TEXT("pushable was held without the observed focused interact input"); return EVBPlayBotOpeningResult::Failed; }
			PushPhase = EPushPhase::Push; PushBest = View.TargetDisplacement; PushProgressSeconds = Seconds;
			return EVBPlayBotOpeningResult::Running;
		}
		if (PressId != INDEX_NONE)
		{
			Input.SetLookWant(FVector2D::ZeroVector);
			if (Seconds-ActSeconds > EffectTimeout) { Error = TEXT("interact input did not grab the pushable"); return EVBPlayBotOpeningResult::Failed; }
			return EVBPlayBotOpeningResult::Running;
		}
		if (!View.bGrounded) { Input.SetLookWant(FVector2D::ZeroVector); return EVBPlayBotOpeningResult::Running; }
		if (View.bFocused)
		{
			bSawFocus = true; Input.SetLookWant(FVector2D::ZeroVector);
			if (!Schedule(Action,Seconds,Input,Human)) { Error = TEXT("native push grab schedule was rejected"); return EVBPlayBotOpeningResult::Failed; }
			return EVBPlayBotOpeningResult::Running;
		}
		LookAtTarget(View,Input);
		return EVBPlayBotOpeningResult::Running;

	case EPushPhase::Push:
		// 잡은 동안 패드 앞 방향(스틱 Y)이 밀기 입력이다(UVBHeroComponent -> SetPushInput(MoveValue.Y)).
		Input.SetLookWant(FVector2D::ZeroVector);
		if (!View.bHolding) { Error = TEXT("pushable was let go before the declared distance"); return EVBPlayBotOpeningResult::Failed; }
		if (View.TargetDisplacement >= Step.PushMinDistance)
		{
			Input.SetStickWant(FVector2D::ZeroVector);
			PushPhase = EPushPhase::Release; PressId = INDEX_NONE; bInjected = false;
			return EVBPlayBotOpeningResult::Running;
		}
		if (View.TargetDisplacement > PushBest + 1.0) { PushBest = View.TargetDisplacement; PushProgressSeconds = Seconds; }
		if (Seconds-PushProgressSeconds > EffectTimeout)
		{
			Error = FString::Printf(TEXT("push input stopped moving the target at %.0f of %.0f cm"), PushBest, Step.PushMinDistance);
			return EVBPlayBotOpeningResult::Failed;
		}
		Input.SetStickWant(FVector2D(0.0, Human.GetProfile().MoveMagnitude));
		return EVBPlayBotOpeningResult::Running;

	case EPushPhase::Release:
		Input.SetStickWant(FVector2D::ZeroVector); Input.SetLookWant(FVector2D::ZeroVector);
		if (!View.bHolding)
		{
			if (!bInjected) { Error = TEXT("pushable was released without the observed interact input"); return EVBPlayBotOpeningResult::Failed; }
			if (CompleteSeconds < 0.0) { CompleteSeconds = Seconds; }
			return Seconds-CompleteSeconds >= 0.3 ? EVBPlayBotOpeningResult::PartialComplete : EVBPlayBotOpeningResult::Running;
		}
		CompleteSeconds = -1.0;
		if (PressId == INDEX_NONE)
		{
			if (!Schedule(Action,Seconds,Input,Human)) { Error = TEXT("native push release schedule was rejected"); return EVBPlayBotOpeningResult::Failed; }
			return EVBPlayBotOpeningResult::Running;
		}
		if (Seconds-ActSeconds > EffectTimeout) { Error = TEXT("interact input did not release the pushable"); return EVBPlayBotOpeningResult::Failed; }
		return EVBPlayBotOpeningResult::Running;
	}
	return EVBPlayBotOpeningResult::Failed;
}
