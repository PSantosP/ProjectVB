// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Debug/VBCombatReadoutSubsystem.h"

#include "Data/VBComboSelectionPolicy.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "VB/VB.h"

namespace
{
	TAutoConsoleVariable<int32> CVarCombatReadout(
		TEXT("vb.Combat.Readout"), 0,
		TEXT("1 = dev on-screen combat readout: combo choice + reason, distance per hit window vs stop distance, HIT/MISS, knockback estimate vs measured displacement."),
		ECVF_Cheat);

	// 피격자의 수평 속도가 이 값 아래로 떨어지거나 MaxTrackSeconds 가 지나면 밀림이 끝난 것으로 본다.
	constexpr float SettledSpeed = 10.0f;
	constexpr double MinTrackSeconds = 0.05;
	constexpr double MaxTrackSeconds = 1.5;
	// 화면 줄에 key 를 붙여 스크롤 대신 매 프레임 덮어쓰게 한다.
	constexpr uint64 KeyBase = 0x5642524541444F00ull;
	constexpr int32 MaxDrawnLines = 40;

	const TCHAR* RoleName(EVBComboRole Role)
	{
		switch (Role)
		{
		case EVBComboRole::Close: return TEXT("Close");
		case EVBComboRole::Far: return TEXT("Far");
		case EVBComboRole::Sprint: return TEXT("Sprint");
		case EVBComboRole::Manual: return TEXT("Manual");
		default: return TEXT("Neutral");
		}
	}

	FString ShortName(const AActor* Actor)
	{
		return Actor ? Actor->GetActorNameOrLabel() : TEXT("?");
	}
}

// ---- FVBCombatReadoutLog ----

FVBCombatReadoutLog::FComboBlock* FVBCombatReadoutLog::FindCombo(const AActor* Attacker)
{
	return Combos.FindByPredicate([Attacker](const FComboBlock& Block) { return Block.Attacker.Get() == Attacker; });
}

FVBCombatReadoutLog::FHitLine* FVBCombatReadoutLog::FindLine(const AActor* Attacker, int32 WindowKey)
{
	FComboBlock* Block = FindCombo(Attacker);
	if (!Block)
	{
		return nullptr;
	}
	// 최신부터 본다 - 재사용한 블록에 같은 key 의 옛 줄이 있을 수 있다.
	for (int32 Index = Block->Hits.Num() - 1; Index >= 0; --Index)
	{
		if (Block->Hits[Index].WindowKey == WindowKey)
		{
			return &Block->Hits[Index];
		}
	}

	return nullptr;
}

void FVBCombatReadoutLog::AddCombo(const AActor* Attacker, const FString& Header, double Now)
{
	FComboBlock Block;
	Block.Attacker = Attacker;
	Block.Header = Header;
	Block.StartTime = Now;
	Combos.Insert(MoveTemp(Block), 0);
	if (Combos.Num() > MaxCombos)
	{
		Combos.SetNum(MaxCombos);
	}
}

void FVBCombatReadoutLog::WindowOpen(const AActor* Attacker, int32 WindowKey, int32 HitNumber, const FString& Shape, float DistanceCm,
	float StopCm, const AActor* Target)
{
	FComboBlock* Block = FindCombo(Attacker);
	if (!Block)
	{
		// 판독기를 켜기 전에 콤보가 시작됐거나, 공격에 선택 정책이 없다.
		AddCombo(Attacker, FString::Printf(TEXT("COMBO %s  (no selection record)"), *ShortName(Attacker)), 0.0);
		Block = &Combos[0];
	}
	// 새 창은 추적 중인 밀림을 끝내지 않는다 - 같은 피격자의 다음 밀림(Knockback)이나 멈춤이 끝낸다.
	if (Block->Hits.Num() >= MaxHitsPerCombo)
	{
		Block->Hits.RemoveAt(0);
	}

	FHitLine& Line = Block->Hits.AddDefaulted_GetRef();
	Line.WindowKey = WindowKey;
	Line.HitNumber = HitNumber;
	Line.Shape = Shape;
	Line.DistanceCm = DistanceCm;
	Line.StopCm = StopCm;
	Line.Target = Target;
}

void FVBCombatReadoutLog::WindowHit(const AActor* Attacker, int32 WindowKey, const AActor* Victim)
{
	FHitLine* Line = FindLine(Attacker, WindowKey);
	if (Line && Victim && !Line->Victims.ContainsByPredicate([Victim](const FVictimLine& V) { return V.Actor.Get() == Victim; }))
	{
		FVictimLine& Entry = Line->Victims.AddDefaulted_GetRef();
		Entry.Actor = Victim;
		Entry.Name = ShortName(Victim);
	}
}

FString FVBCombatReadoutLog::WindowClosed(const AActor* Attacker, int32 WindowKey, bool bCut)
{
	FComboBlock* Block = FindCombo(Attacker);
	FHitLine* Line = FindLine(Attacker, WindowKey);
	if (!Block || !Line || Line->bClosed)
	{
		return FString();
	}

	Line->bClosed = true;
	Line->bCut = bCut && Line->Victims.IsEmpty();
	// 타격감 규칙 - 이 콤보의 앞 타격이 맞힌 적을 창이 놓쳤다면 앞 타격이 적을 사거리 밖으로 보낸 것이다.
	//   앞 줄과 같은 대상만 보고, 끊긴 창은 아무것도 말하지 않는다.
	if (Line->Victims.IsEmpty() && !Line->bCut && Line->Target.IsValid())
	{
		const int32 LineIndex = static_cast<int32>(Line - Block->Hits.GetData());
		for (int32 Index = LineIndex - 1; Index >= 0; --Index)
		{
			const FHitLine& Earlier = Block->Hits[Index];
			const AActor* Target = Line->Target.Get();
			if (Earlier.Victims.ContainsByPredicate([Target](const FVictimLine& V) { return V.Actor.Get() == Target; }))
			{
				Line->MissAfterHit = Earlier.HitNumber;
				break;
			}
		}
	}

	return FormatLine(*Line);
}

void FVBCombatReadoutLog::Knockback(const AActor* Victim, const AActor* Instigator, float Strength, float EstimatedCm,
	const TCHAR* Mode, double Now)
{
	FComboBlock* Block = FindCombo(Instigator);
	if (!Block || !Victim)
	{
		return;
	}
	// 이 피격자에게 진행 중인 밀림은 여기서 끝난다 - 새 타격이 지금 위치에서 새 밀림을 시작한다.
	for (FHitLine& Earlier : Block->Hits)
	{
		for (FVictimLine& Entry : Earlier.Victims)
		{
			if (Entry.bTracking && Entry.Actor.Get() == Victim)
			{
				EndPush(*Block, Earlier, Entry, Now, TEXT("cut by the next push"));
			}
		}
	}
	// 이 피격자를 기록한 가장 새 줄이 넉백을 낸 타격이다.
	for (int32 Index = Block->Hits.Num() - 1; Index >= 0; --Index)
	{
		for (FVictimLine& Entry : Block->Hits[Index].Victims)
		{
			if (Entry.Actor.Get() == Victim)
			{
				Entry.Strength = Strength;
				Entry.EstimatedCm = EstimatedCm;
				Entry.Mode = Mode;
				Entry.Start = Victim->GetActorLocation();
				Entry.TrackStart = Now;
				Entry.MovedCm = -1.0f;
				Entry.bTracking = true;
				return;
			}
		}
	}
}

void FVBCombatReadoutLog::StopTracking(FVictimLine& Victim)
{
	if (!Victim.bTracking)
	{
		return;
	}

	Victim.bTracking = false;
	if (const AActor* Actor = Victim.Actor.Get())
	{
		Victim.MovedCm = FVector::Dist2D(Actor->GetActorLocation(), Victim.Start);
	}
}

void FVBCombatReadoutLog::TickTracking(double Now)
{
	for (FComboBlock& Block : Combos)
	{
		for (FHitLine& Line : Block.Hits)
		{
			for (FVictimLine& Victim : Line.Victims)
			{
				if (!Victim.bTracking)
				{
					continue;
				}

				const AActor* Actor = Victim.Actor.Get();
				const double Elapsed = Now - Victim.TrackStart;
				const bool bSettled = Actor && Elapsed > MinTrackSeconds && Actor->GetVelocity().Size2D() < SettledSpeed;
				if (!Actor || bSettled || Elapsed > MaxTrackSeconds)
				{
					EndPush(Block, Line, Victim, Now, !Actor ? TEXT("victim gone") : bSettled ? TEXT("settled") : TEXT("timeout"));
				}
			}
		}
	}
}

void FVBCombatReadoutLog::EndPush(const FComboBlock& Block, const FHitLine& Line, FVictimLine& Victim, double Now, const TCHAR* Reason)
{
	StopTracking(Victim);
	PendingPushLines.Add(FString::Printf(TEXT("PUSH %s hit %d -> %s: kb %.0f %s est %.0fcm moved %.0fcm in %.2fs (%s)"),
		*ShortName(Block.Attacker.Get()), Line.HitNumber, *Victim.Name, Victim.Strength, *Victim.Mode, Victim.EstimatedCm,
		FMath::Max(0.0f, Victim.MovedCm), Now - Victim.TrackStart, Reason));
}

TArray<FString> FVBCombatReadoutLog::TakePushLines()
{
	return MoveTemp(PendingPushLines);
}

FString FVBCombatReadoutLog::FormatLine(const FHitLine& Line)
{
	FString Text = FString::Printf(TEXT(" hit %d %s  open: dist %.0f (stop %.0f)  "), Line.HitNumber, *Line.Shape, Line.DistanceCm,
		Line.StopCm);
	if (!Line.bClosed)
	{
		return Text + (Line.Victims.IsEmpty() ? TEXT("open") : TEXT("open, contact"));
	}

	if (Line.bCut)
	{
		return Text + TEXT("cut (interrupted before contact)");
	}

	if (Line.Victims.IsEmpty())
	{
		Text += TEXT("MISS");
		if (Line.MissAfterHit > 0)
		{
			Text += FString::Printf(TEXT("  <- enemy outside reach after hit %d"), Line.MissAfterHit);
		}

		return Text;
	}

	Text += TEXT("HIT");
	for (const FVictimLine& Victim : Line.Victims)
	{
		Text += FString::Printf(TEXT("  %s"), *Victim.Name);
		if (Victim.Strength >= 0.0f)
		{
			Text += FString::Printf(TEXT(" kb %.0f %s est %.0fcm"), Victim.Strength, *Victim.Mode, Victim.EstimatedCm);
			Text += Victim.MovedCm >= 0.0f ? FString::Printf(TEXT(" moved %.0fcm"), Victim.MovedCm) : FString(TEXT(" moving.."));
		}
	}

	return Text;
}

TArray<TPair<FString, FColor>> FVBCombatReadoutLog::BuildLines() const
{
	TArray<TPair<FString, FColor>> Lines;
	for (const FComboBlock& Block : Combos)
	{
		Lines.Emplace(Block.Header, FColor::Cyan);
		for (const FHitLine& Line : Block.Hits)
		{
			const FColor Color = !Line.bClosed || Line.bCut ? FColor::Silver
				: Line.Victims.IsEmpty() ? (Line.MissAfterHit > 0 ? FColor::Red : FColor::Orange)
				: FColor::Green;
			Lines.Emplace(FormatLine(Line), Color);
		}
	}

	return Lines;
}

// ---- UVBCombatReadoutSubsystem ----

UVBCombatReadoutSubsystem* UVBCombatReadoutSubsystem::GetActive(const UObject* WorldContext)
{
#if UE_BUILD_SHIPPING
	return nullptr;
#else
	if (CVarCombatReadout.GetValueOnGameThread() == 0 || !WorldContext)
	{
		return nullptr;
	}

	const UWorld* World = WorldContext->GetWorld();
	return World ? World->GetSubsystem<UVBCombatReadoutSubsystem>() : nullptr;
#endif
}

bool UVBCombatReadoutSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return Super::ShouldCreateSubsystem(Outer);
#endif
}

bool UVBCombatReadoutSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UVBCombatReadoutSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVBCombatReadoutSubsystem, STATGROUP_Tickables);
}

double UVBCombatReadoutSubsystem::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

FString UVBCombatReadoutSubsystem::DescribeChoice(const AActor* Attacker, const FString& VariantName, const FVBComboChoice& Choice,
	const FVBComboSelectContext& Context)
{
	FString Reason;
	switch (Choice.Reason)
	{
	case EVBComboChoiceReason::Sprint:
		Reason = TEXT("sprint");
		break;
	case EVBComboChoiceReason::Band:
		Reason = FString::Printf(TEXT("band %s (dist %.0f)"), RoleName(Choice.Band), Context.DistanceCm);
		break;
	case EVBComboChoiceReason::BandNeutral:
		Reason = FString::Printf(TEXT("band %s had none in reach -> Neutral (dist %.0f)"), RoleName(Choice.Band), Context.DistanceCm);
		break;
	case EVBComboChoiceReason::NoTarget:
		Reason = TEXT("no target");
		break;
	case EVBComboChoiceReason::NoneInReach:
		Reason = FString::Printf(TEXT("fallback: none in reach (dist %.0f)"), Context.DistanceCm);
		break;
	case EVBComboChoiceReason::Forced:
		Reason = FString::Printf(TEXT("FORCED by vb.Combat.ForceCombo (dist %.0f)"), Context.DistanceCm);
		break;
	default:
		Reason = TEXT("no candidates");
		break;
	}

	Reason += FString::Printf(TEXT(", %d qualified"), Choice.Qualified);
	if (Choice.RotationRole.IsSet())
	{
		Reason += Choice.RotationAfter == INDEX_NONE
			? FString::Printf(TEXT(", rotation %s first"), RoleName(Choice.RotationRole.GetValue()))
			: FString::Printf(TEXT(", rotation %s after #%d"), RoleName(Choice.RotationRole.GetValue()), Choice.RotationAfter);
	}

	return FString::Printf(TEXT("COMBO %s #%d %s  reason: %s  %s"), *ShortName(Attacker), Choice.Variant, *VariantName, *Reason,
		Choice.bTargetInReach ? TEXT("in reach") : TEXT("NOT in reach"));
}

void UVBCombatReadoutSubsystem::NoteComboChosen(const AActor* Attacker, const FString& VariantName, const FVBComboChoice& Choice,
	const FVBComboSelectContext& Context)
{
	const FString Header = DescribeChoice(Attacker, VariantName, Choice, Context);
	VB_LOG(Log, "[Readout] %s", *Header);
	Book.AddCombo(Attacker, Header, Now());
}

void UVBCombatReadoutSubsystem::NoteDodgeResume(const AActor* Attacker, const FString& VariantName)
{
	const FString Header = FString::Printf(TEXT("COMBO %s %s  reason: dodge resume (same variant)"), *ShortName(Attacker), *VariantName);
	VB_LOG(Log, "[Readout] %s", *Header);
	Book.AddCombo(Attacker, Header, Now());
}

void UVBCombatReadoutSubsystem::NoteWindowOpen(const AActor* Attacker, int32 WindowKey, int32 HitNumber, const FString& Shape,
	float DistanceCm, float StopCm, const AActor* Target)
{
	Book.WindowOpen(Attacker, WindowKey, HitNumber, Shape, DistanceCm, StopCm, Target);
}

void UVBCombatReadoutSubsystem::NoteWindowHits(const AActor* Attacker, int32 WindowKey, const TArray<FHitResult>& Hits)
{
	for (const FHitResult& Hit : Hits)
	{
		Book.WindowHit(Attacker, WindowKey, Hit.GetActor());
	}
}

void UVBCombatReadoutSubsystem::NoteWindowClosed(const AActor* Attacker, int32 WindowKey, bool bCut)
{
	const FString Line = Book.WindowClosed(Attacker, WindowKey, bCut);
	if (!Line.IsEmpty())
	{
		VB_LOG(Log, "[Readout] %s %s", *ShortName(Attacker), *Line);
	}
}

void UVBCombatReadoutSubsystem::NoteKnockback(const AActor* Victim, const AActor* Instigator, float Strength, float EstimatedCm,
	const TCHAR* Mode)
{
	Book.Knockback(Victim, Instigator, Strength, EstimatedCm, Mode, Now());
}

void UVBCombatReadoutSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (CVarCombatReadout.GetValueOnGameThread() == 0 || !GEngine)
	{
		return;
	}

	Book.TickTracking(Now());
	for (const FString& Push : Book.TakePushLines())
	{
		VB_LOG(Log, "[Readout] %s", *Push);
	}

	int32 Drawn = 0;
	const auto Put = [&Drawn](const FString& Text, const FColor& Color)
	{
		if (Drawn < MaxDrawnLines)
		{
			// key 를 거꾸로 세어 최신 콤보가 위에 찍힌다. 수명 0.0초 = 매 프레임 다시 그린다.
			GEngine->AddOnScreenDebugMessage(KeyBase + (MaxDrawnLines - Drawn), 0.0f, Color, Text);
			++Drawn;
		}
	};
	Put(TEXT("vb.Combat.Readout (server data; client PIE windows show nothing)"), FColor::White);
	for (const TPair<FString, FColor>& Line : Book.BuildLines())
	{
		Put(Line.Key, Line.Value);
	}
}
