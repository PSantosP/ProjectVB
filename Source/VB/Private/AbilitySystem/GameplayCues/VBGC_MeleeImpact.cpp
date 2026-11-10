// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_MeleeImpact.h"
#include "AbilitySystem/GameplayCues/VBImpactMarkSubsystem.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "CharacterCommon/VBBodyContact.h"

#include "Components/DecalComponent.h"
#include "Data/VBWeaponDefinition.h"
#include "Data/VBWeaponHitVisuals.h"
#include "GameplayEffectTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "Settings/VBOptionsSettings.h"
#include "Types/VBNiagaraParameters.h"
#include "Types/VBReportOnce.h"
#include "VB/VB.h"

FQuat FVBMeleeImpactFrame::ToQuat() const
{
	return FRotationMatrix::MakeFromXZ(Normal, SwingAxis).ToQuat();
}

FVBMeleeImpactFrame UVBGC_MeleeImpact::ResolveImpactFrame(const FHitResult* Hit, const FVector& TargetLocation,
	const FVector& SourceLocation, float SurfaceOffset)
{
	FVBMeleeImpactFrame Frame;
	Frame.bFromHitResult = Hit != nullptr;
	Frame.Location = Hit ? FVector(Hit->ImpactPoint) : TargetLocation;

	// 법선: 접점 면의 법선이 먼저다. 처음부터 겹친 스윕은 법선이 0 일 수 있어 "맞은 쪽 -> 때린 쪽" 으로 대신한다.
	FVector Normal = Hit ? FVector(Hit->ImpactNormal).GetSafeNormal() : FVector::ZeroVector;
	if (Normal.IsNearlyZero())
	{
		Normal = (SourceLocation - TargetLocation).GetSafeNormal();
	}

	if (Normal.IsNearlyZero())
	{
		Normal = FVector::ForwardVector;
	}

	Frame.Normal = Normal;

	// 날의 이동이 0 이면 "옆으로 벤 것"(가장 흔한 수평 베기)으로 본다.
	FVector Swing = Hit ? FVector(Hit->TraceEnd - Hit->TraceStart).GetSafeNormal() : FVector::ZeroVector;
	if (Swing.IsNearlyZero())
	{
		Swing = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
		if (Swing.IsNearlyZero())
		{
			Swing = FVector::RightVector;
		}
	}

	Frame.SwingDirection = Swing;

	// 회전용 +Z 는 법선에 직교해야 한다. 찌르기(날이 법선과 평행)는 직교 성분이 없으니 위쪽을, 그것도 평행이면 옆을 쓴다.
	FVector Axis = Swing - FVector::DotProduct(Swing, Normal) * Normal;
	if (Axis.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		Axis = FVector::UpVector - FVector::DotProduct(FVector::UpVector, Normal) * Normal;
	}

	if (Axis.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		Axis = FVector::RightVector - FVector::DotProduct(FVector::RightVector, Normal) * Normal;
	}

	Frame.SwingAxis = Axis.GetSafeNormal();

	Frame.Location += Normal * FMath::Max(0.0f, SurfaceOffset);
	return Frame;
}

void UVBGC_MeleeImpact::SpawnGroundDecal(UWorld& World, const FVBMeleeImpactFrame& Frame, const AActor* HitActor, const AActor* Source,
	EVBBloodDisplay BloodDisplay) const
{
	if (GroundTraceDistance <= 0.0f)
	{
		return;
	}
	// 맞은 캐릭터와 때린 캐릭터는 건너뛴다 - 캡슐에 맞으면 "바닥" 이 허리 높이가 된다.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBMeleeImpactGround), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(HitActor);
	Params.AddIgnoredActor(Source);
	FHitResult Ground;
	const FVector Start = Frame.Location;
	if (!World.LineTraceSingleByChannel(Ground, Start, Start - FVector::UpVector * GroundTraceDistance, ECC_Visibility, Params))
	{
		return; // 공중, 낭떠러지 위 - 남길 바닥이 없다(정상)
	}

	UMaterialInterface* Material = GroundDecalMaterials[FMath::RandRange(0, GroundDecalMaterials.Num() - 1)];
	if (!Material)
	{
		return;
	}
	// 다른 색인데 재질이 색 바꾸기(TintReplace)를 모르면 붉은 자국이 그대로 남는다 - 남기지 않고 알린다.
	float ReplaceDefault = 0.0f;
	if (BloodDisplay == EVBBloodDisplay::Alternate
		&& !Material->GetScalarParameterValue(FHashedMaterialParameterInfo(DecalTintReplaceParameter), ReplaceDefault))
	{
		VB_LOG(Warning, "VBGC_MeleeImpact[%s]: 데칼 재질 %s 에 %s 가 없어 피 표현 «다른 색» 자국을 남기지 않음",
			*GameplayCueTag.ToString(), *Material->GetName(), *DecalTintReplaceParameter.ToString());
		return;
	}

	const float Radius = FMath::FRandRange(GroundDecalRadiusRange.X, FMath::Max(GroundDecalRadiusRange.X, GroundDecalRadiusRange.Y));
	// 데칼은 자기 -X 로 투영한다. 바닥 법선을 X 로 두고(위를 본다) 그 축으로 돌려 매번 다른 모양처럼 보이게 한다.
	FRotator Rotation = FRotationMatrix::MakeFromX(-FVector(Ground.ImpactNormal)).Rotator();
	Rotation.Roll = FMath::FRandRange(0.0f, 360.0f);
	const float Total = GroundDecalLifetime + GroundDecalFadeDuration;
	UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(&World, Material, FVector(GroundDecalDepth, Radius, Radius),
		Ground.ImpactPoint, Rotation, Total > 0.0f ? Total + 0.1f : 0.0f);
	if (Decal && BloodDisplay == EVBBloodDisplay::Alternate)
	{
		// 자국마다 동적 재질 하나 - 자국 수는 MaxGroundDecals 가 막는다.
		if (UMaterialInstanceDynamic* Recolored = Decal->CreateDynamicMaterialInstance())
		{
			Recolored->SetVectorParameterValue(DecalTintParameter, GetDefault<UVBOptionsSettings>()->AlternateBloodDecalColor);
			Recolored->SetScalarParameterValue(DecalTintReplaceParameter, 1.0f);
		}
	}

	if (Decal && GroundDecalFadeDuration > 0.0f)
	{
		Decal->SetFadeOut(GroundDecalLifetime, GroundDecalFadeDuration, /*DestroyOwnerAfterFade=*/false);
		Decal->SetFadeScreenSize(0.001f); // 아주 멀면 그리지 않는다 - 데칼이 쌓이는 전투에서 픽셀 비용을 줄인다
	}

	if (UVBImpactMarkSubsystem* Marks = Decal ? UVBImpactMarkSubsystem::Get(&World) : nullptr)
	{
		Marks->Track(Decal, TEXT("GroundDecal"), MaxGroundDecals);
	}
}

TMap<FName, FLinearColor> UVBGC_MeleeImpact::ResolveBloodColors(EVBBloodDisplay Mode, const TMap<FName, FLinearColor>& CueColors,
	const TMap<FName, FLinearColor>& AlternateColors)
{
	TMap<FName, FLinearColor> Colors = CueColors;
	if (Mode == EVBBloodDisplay::Alternate || Mode == EVBBloodDisplay::Off)
	{
		Colors.Append(AlternateColors); // 같은 이름이면 대체 색이 이긴다
	}

	return Colors;
}

bool UVBGC_MeleeImpact::ShowsBlood(EVBBloodDisplay Mode)
{
	return Mode != EVBBloodDisplay::Off;
}

void UVBGC_MeleeImpact::SpawnWound(const FVBMeleeImpactFrame& Frame, const FHitResult& Hit, const TMap<FName, FLinearColor>& Colors) const
{
	// Hit 는 표면을 찾은 VBBodyContact::RefineOntoBody 를 거친 것 - 컴포넌트는 바디 메시, ImpactPoint 는 메시 표면, BoneName 은 그 표면의 뼈다.
	USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(Hit.GetComponent());
	if (!Mesh)
	{
		return; // 메시 없는 대상(소품 등) - 상처를 붙일 메시가 없다(정상)
	}

	const FVector Surface = FVector(Hit.ImpactPoint);
	const FVector Normal = Frame.Normal;
	FName Bone = Hit.BoneName;
	if (Bone.IsNone() || Mesh->GetBoneIndex(Bone) == INDEX_NONE)
	{
		Bone = Mesh->FindClosestBone(Surface);
	}

	if (Bone.IsNone())
	{
		return;
	}

	const FVector Location = Surface + Normal * WoundSurfaceOffset;

	// 붙인다(KeepWorldPosition): 그 자리, 그 방향 그대로 뼈의 자식이 된다. 오래 사는 이펙트라 풀링하지 않는다 - 개수는 월드 상한이 막는다.
	UNiagaraComponent* Wound = UNiagaraFunctionLibrary::SpawnSystemAttached(
		WoundSystem, Mesh, Bone, Location, Frame.ToQuat().Rotator(), FVector(WoundScale), EAttachLocation::KeepWorldPosition,
		/*bAutoDestroy=*/true, ENCPoolMethod::None, /*bAutoActivate=*/false, /*bPreCullCheck=*/false);
	if (Wound)
	{
		if (UVBImpactMarkSubsystem* Marks = UVBImpactMarkSubsystem::Get(Mesh))
		{
			Marks->Track(Wound, TEXT("Wound"), MaxWounds);
		}

		Wound->SetVariableVec3(HitNormalParameter, Normal);
		Wound->SetVariableVec3(HitDirectionParameter, Frame.SwingDirection);
		for (const TPair<FName, FLinearColor>& Pair : Colors)
		{
			Wound->SetVariableLinearColor(Pair.Key, Pair.Value);
		}

		Wound->Activate(true);
	}
}

const UVBWeaponHitVisuals* UVBGC_MeleeImpact::ResolveHitVisuals(const FGameplayEffectContextHandle& Context)
{
	const UVBWeaponDefinition* Weapon = Cast<UVBWeaponDefinition>(Context.GetSourceObject());
	if (!Weapon)
	{
		return nullptr; // 적, 근접이 아닌 피해 - 큐 자신의 모습
	}

	const UVBWeaponHitVisuals* Visuals = Weapon->GetHitVisuals();
	if (!Visuals)
	{
		if (VBFirstReportFor(Weapon))
		{
			VB_LOG(Log, "VBGC_MeleeImpact: %s 에 HitVisuals 가 없어 타격 큐 자신의 모습으로 낸다", *Weapon->GetName());
		}
	}

	return Visuals;
}

UVBGC_MeleeImpact::FLayers UVBGC_MeleeImpact::PlanLayers(const UVBWeaponHitVisuals* Visuals, bool bShowBlood, bool bOnBody,
	const FGameplayTag& StrikeShape, EVBHitImpact Impact)
{
	FLayers Layers;
	if (!Visuals)
	{
		return Layers;
	}

	Layers.Ripple = Visuals->RippleSystem.Get();
	Layers.CutLine = bOnBody ? Visuals->CutLineSystem.Get() : nullptr;
	Layers.Blood = bShowBlood ? Visuals->BloodSystem.Get() : nullptr;
	for (const FVBStrikeVisualRow& Row : Visuals->StrikeRows)
	{
		if (Row.System && Row.Matches(StrikeShape, Impact))
		{
			Layers.Strikes.Add(&Row);
		}
	}

	return Layers;
}

void UVBGC_MeleeImpact::SpawnWeaponLayers(UWorld& World, const UVBWeaponHitVisuals& Visuals, const FLayers& Layers,
	const FVBMeleeImpactFrame& Frame, const FHitResult* Hit, const TMap<FName, FLinearColor>& Colors) const
{
	// 값은 켜기 전에 넣는다 - 즉시 버스트는 켜는 순간 스폰 스크립트가 돈다.
	const FRotator Rotation = Frame.ToQuat().Rotator();
	if (Layers.Ripple)
	{
		if (UNiagaraComponent* Ripple = UNiagaraFunctionLibrary::SpawnSystemAtLocation(&World, Layers.Ripple,
			Frame.Location, Rotation, FVector::OneVector, true, false, ENCPoolMethod::AutoRelease, true))
		{
			Ripple->SetVariableFloat(UVBWeaponHitVisuals::RippleScaleParameter, Visuals.RippleScale);
			Ripple->SetVariableFloat(UVBWeaponHitVisuals::RippleDurationParameter, Visuals.RippleDuration);
			Ripple->SetVariableLinearColor(UVBWeaponHitVisuals::RippleColorParameter, Visuals.RippleColor);
			Ripple->Activate(true);
		}
	}

	if (Layers.Blood)
	{
		if (UNiagaraComponent* Blood = UNiagaraFunctionLibrary::SpawnSystemAtLocation(&World, Layers.Blood,
			Frame.Location, Rotation, FVector(EffectScale), true, false, ENCPoolMethod::AutoRelease, true))
		{
			Blood->SetVariableVec3(HitNormalParameter, Frame.Normal);
			Blood->SetVariableVec3(HitDirectionParameter, Frame.SwingDirection);
			Blood->SetVariableVec3(HitVelocityParameter, Frame.SwingDirection * SwingSpeed);
			for (const TPair<FName, FLinearColor>& Pair : Colors)
			{
				Blood->SetVariableLinearColor(Pair.Key, Pair.Value);
			}

			Blood->Activate(true);
		}
	}

	for (const FVBStrikeVisualRow* Row : Layers.Strikes)
	{
		if (UNiagaraComponent* Strike = UNiagaraFunctionLibrary::SpawnSystemAtLocation(&World, Row->System.Get(),
			Frame.Location, Rotation, FVector::OneVector, true, false, ENCPoolMethod::AutoRelease, true))
		{
			Strike->SetVariableFloat(UVBWeaponHitVisuals::StrikeScaleParameter, Row->Scale);
			Strike->SetVariableLinearColor(UVBWeaponHitVisuals::StrikeColorParameter, Row->Color);
			Strike->Activate(true);
		}
	}
	// 베인 자국은 맞은 뼈에 붙어 메시를 따라간다 - 메시 표면이 있을 때만 PlanLayers 가 골랐다.
	USkeletalMeshComponent* Mesh = (Layers.CutLine && Hit) ? Cast<USkeletalMeshComponent>(Hit->GetComponent()) : nullptr;
	const FName Bone = Mesh
		? (Mesh->GetBoneIndex(Hit->BoneName) != INDEX_NONE ? Hit->BoneName : Mesh->FindClosestBone(Hit->ImpactPoint)) : NAME_None;
	if (Mesh && !Bone.IsNone())
	{
		if (UNiagaraComponent* Cut = UNiagaraFunctionLibrary::SpawnSystemAttached(Layers.CutLine, Mesh, Bone,
			Frame.Location, Rotation, FVector::OneVector, EAttachLocation::KeepWorldPosition, true, ENCPoolMethod::None, false, false))
		{
			Cut->SetVariableFloat(UVBWeaponHitVisuals::CutLineWidthParameter, Visuals.CutLineWidth);
			Cut->SetVariableFloat(UVBWeaponHitVisuals::CutLineLengthParameter, Visuals.CutLineLength);
			Cut->SetVariableFloat(UVBWeaponHitVisuals::CutLineDurationParameter, Visuals.CutLineDuration);
			Cut->SetVariableLinearColor(UVBWeaponHitVisuals::CutLineColorParameter, Visuals.CutLineColor);
			Cut->Activate(true);
		}
	}
}

bool UVBGC_MeleeImpact::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	UWorld* World = IsValid(MyTarget) ? MyTarget->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	// 근접 타격은 맞은 쪽 ASC, 처형 타격은 공격자 ASC 에서 쏜다. 맞은 액터는 HitResult, 때린 쪽은 Instigator 에 있다.
	// 출혈 폭발은 HitResult 없이 온다.
	const FHitResult* ContextHit = Parameters.EffectContext.GetHitResult();
	const AActor* Source = Parameters.GetInstigator();
	const FVector SourceLocation = Source ? Source->GetActorLocation() : MyTarget->GetActorLocation();
	// 접점을 메시 표면으로 다시 구한 사본으로 틀을 세운다(VBBodyContact::RefineOntoBody). 컨텍스트의 원본은 건드리지 않는다.
	FHitResult Contact;
	const FHitResult* Hit = nullptr;
	bool bOnBody = false;
	if (ContextHit)
	{
		Contact = *ContextHit;
		bOnBody = VBBodyContact::RefineOntoBody(Contact, SourceLocation);
		Hit = &Contact;
	}

	const AActor* HitActor = Hit ? Hit->GetActor() : nullptr;
	const FVector TargetLocation = HitActor ? HitActor->GetActorLocation() : MyTarget->GetActorLocation();
	const FVBMeleeImpactFrame Frame = ResolveImpactFrame(Hit, TargetLocation, SourceLocation, SurfaceOffset);
	if (!Frame.bFromHitResult)
	{
		// Warning 이 아닌 이유: 접점 없는 경로(출혈 폭발)는 정상이다. 근접 타격에서 이 줄이 보이면 문맥 HitResult 가 빠진 것이다.
		VB_LOG(Verbose, "VBGC_MeleeImpact[%s]: HitResult 없음 - 대상 위치에서 대체 방향으로 터뜨림", *GameplayCueTag.ToString());
	}

	// 옵션 "피 표현". 캐시하지 않는다 - 옵션을 바꾸면 다음 타격부터 바로 따른다.
	const EVBBloodDisplay BloodDisplay = UVBSaveGameSubsystem::GetSettingsData(MyTarget).BloodDisplay;
	const bool bShowBlood = ShowsBlood(BloodDisplay);
	const TMap<FName, FLinearColor> Colors = ResolveBloodColors(BloodDisplay, ColorParameters,
		GetDefault<UVBOptionsSettings>()->AlternateBloodColors);

	SpawnImpactEffect(*World, Parameters.EffectContext, Frame, Hit, bOnBody, bShowBlood, Colors);

	// 메시 표면을 못 찾은 접점은 공중일 수 있어 상처를 붙이지 않는다. 피 표현 끄기면 상처, 바닥 자국을 만들지 않는다.
	if (WoundSystem && Hit && bOnBody && bShowBlood)
	{
		SpawnWound(Frame, *Hit, Colors);
	}

	if (!GroundDecalMaterials.IsEmpty() && bShowBlood)
	{
		SpawnGroundDecal(*World, Frame, HitActor, Source, BloodDisplay);
	}

	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, ImpactSound, Frame.Location, FRotator::ZeroRotator,
			1.0f, 1.0f, 0.0f, SoundAttenuation, SoundConcurrency);
	}

	return true;
}

void UVBGC_MeleeImpact::SpawnImpactEffect(UWorld& World, const FGameplayEffectContextHandle& Context, const FVBMeleeImpactFrame& Frame,
	const FHitResult* Hit, bool bOnBody, bool bShowBlood, const TMap<FName, FLinearColor>& Colors) const
{
	// 무기가 이 타격에 줄 접점 이펙트가 있으면 그것(울렁임, 베인 자국, 피 터짐, 부위 이펙트), 없으면 이 큐 자신의 모습.
	const UVBWeaponHitVisuals* Visuals = ResolveHitVisuals(Context);
	const FVBGameplayEffectContext* HitFacts = FVBGameplayEffectContext::Get(Context);
	const FLayers Layers = PlanLayers(Visuals, bShowBlood, Hit && bOnBody, HitFacts ? HitFacts->GetStrikingShape() : FGameplayTag(),
		HitFacts ? HitFacts->GetDeclaredHitImpact() : EVBHitImpact::Light);
	if (UsesWeaponLayers(Visuals, Layers))
	{
		SpawnWeaponLayers(World, *Visuals, Layers, Frame, Hit, Colors);
	}
	else
	{
		SpawnCueEffect(World, Frame, bShowBlood, Colors);
	}
}

void UVBGC_MeleeImpact::SpawnCueEffect(UWorld& World, const FVBMeleeImpactFrame& Frame, bool bShowBlood,
	const TMap<FName, FLinearColor>& Colors) const
{
	// 끄기인데 이펙트에 피 스위치가 없으면 값이 조용히 무시돼 피가 나온다 - 이펙트째 건너뛰고 경고한다(CDO 라 매번).
	const bool bBloodSwitchMissing = ImpactSystem && !bShowBlood && !VBNiagaraParameters::HasUserFloat(*ImpactSystem,
		BloodSpawnParameter);
	if (bBloodSwitchMissing)
	{
		VB_LOG(Warning, "VBGC_MeleeImpact[%s]: %s 에 %s 가 없어 피 표현 «끄기» 에서 이펙트를 내지 않음(tools/vfx-lab/wire_blood_switch.py)",
			*GameplayCueTag.ToString(), *ImpactSystem->GetName(), *BloodSpawnParameter.ToString());
	}

	if (ImpactSystem && !bBloodSwitchMissing)
	{
		// 켜기 전에 값을 넣는다 - 즉시 버스트는 켜는 순간 스폰 스크립트가 돈다. 풀링으로 매 타격 생성 비용을 줄인다.
		UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			&World, ImpactSystem, Frame.Location, Frame.ToQuat().Rotator(), FVector(EffectScale),
			/*bAutoDestroy=*/true, /*bAutoActivate=*/false, ENCPoolMethod::AutoRelease, /*bPreCullCheck=*/true);
		if (Effect)
		{
			Effect->SetVariableVec3(HitNormalParameter, Frame.Normal);
			Effect->SetVariableVec3(HitDirectionParameter, Frame.SwingDirection);
			Effect->SetVariableVec3(HitVelocityParameter, Frame.SwingDirection * SwingSpeed);
			for (const TPair<FName, float>& Pair : FloatParameters)
			{
				Effect->SetVariableFloat(Pair.Key, Pair.Value);
			}

			for (const TPair<FName, FLinearColor>& Pair : Colors)
			{
				Effect->SetVariableLinearColor(Pair.Key, Pair.Value);
			}
			// 풀에서 돌아온 컴포넌트는 User 파라미터가 기본값으로 돌아와 있다(UNiagaraComponent::OnPooledReuse) - 그래도 매번 넣는다.
			Effect->SetVariableFloat(BloodSpawnParameter, bShowBlood ? 1.0f : 0.0f);
			Effect->Activate(true);
		}
		// null 은 거리, 가시성 사전 컬링(bPreCullCheck)의 정상 결과라 경고하지 않는다.
	}
	else if (!ImpactSystem)
	{
		VB_LOG(Verbose, "VBGC_MeleeImpact[%s]: ImpactSystem 미지정 - 소리만", *GameplayCueTag.ToString());
	}
}
