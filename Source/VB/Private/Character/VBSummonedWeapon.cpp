#include "Character/VBSummonedWeapon.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "VB/VB.h"

namespace
{
	// 형성 효과와 대기 효과가 공통으로 드러내는 Niagara User 파라미터.
	const FName NiagaraColorParameter(TEXT("User.Color"));
	const FName NiagaraSecondaryColorParameter(TEXT("User.Color Secondary"));
}

AVBSummonedWeapon::AVBSummonedWeapon()
{
	// 틱은 PreviewSummon 이 도는 동안만 켠다. 평소 진행은 UVBWeaponVisualComponent 가 SetManifestProgress 로 넣는다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetReplicates(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot")));
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(RootComponent);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetGenerateOverlapEvents(false);
	WeaponMesh->SetCastShadow(false);
	FormationFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FormationFX"));
	FormationFX->SetupAttachment(RootComponent);
	FormationFX->SetAutoActivate(false);
	IdleFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("IdleFX"));
	IdleFX->SetupAttachment(RootComponent);
	IdleFX->SetAutoActivate(false);
}

void AVBSummonedWeapon::RebuildVisual()
{
	FormationFX->DeactivateImmediate();
	IdleFX->DeactivateImmediate();
	WeaponMesh->SetStaticMesh(VisualPart.Mesh);
	ManifestMaterial = VisualPart.Material ? UMaterialInstanceDynamic::Create(VisualPart.Material, this) : nullptr;
	for (int32 Slot = 0; Slot < WeaponMesh->GetNumMaterials(); ++Slot)
	{
		WeaponMesh->SetMaterial(Slot, ManifestMaterial);
	}

	Reveal = VBWeaponManifest::MeasureReveal(VisualPart.Mesh, VisualPart.RevealAxis);
	if (ManifestMaterial)
	{
		VBWeaponManifest::ApplyReveal(*ManifestMaterial, Reveal);
		ManifestMaterial->SetVectorParameterValue(TEXT("EnergyColor"), VisualPart.Color);
	}

	FormationFX->SetAsset(VisualPart.FormationEffect);
	FormationFX->SetRelativeScale3D(VisualPart.FormationScale);
	IdleFX->SetAsset(VisualPart.IdleEffect);
	for (const auto& Parameter : VisualPart.IdleFloatParameters)
	{
		IdleFX->SetVariableFloat(Parameter.Key, Parameter.Value);
	}

	for (const auto& Parameter : VisualPart.IdleBoolParameters)
	{
		IdleFX->SetVariableBool(Parameter.Key, Parameter.Value);
	}

	IdleFX->SetRelativeLocation(Reveal.BoundsCenter + VisualPart.IdleOffset);
	IdleFX->SetRelativeScale3D(VisualPart.IdleScale);
	BindShape();
	SetManifestProgress(0.0f);
}

void AVBSummonedWeapon::SetShapeBody(USkeletalMeshComponent* Body)
{
	if (ShapeBody.Get() == Body)
	{
		return;
	}

	ShapeBody = Body;
	BindShape();
}

void AVBSummonedWeapon::BindShape()
{
	const FString Parameter = VisualPart.ShapeMeshParameter.ToString();
	for (UNiagaraComponent* Effect : {FormationFX.Get(), IdleFX.Get()})
	{
		if (!Effect || !Effect->GetAsset())
		{
			continue;
		}

		switch (VBResolveWeaponShapeSource(VisualPart))
		{
		case EVBWeaponShapeSource::WeaponMesh:
			// 꺼낸 무기 자신의 모양 - 입자가 칼 표면에서 모이고 흩어진다.
			UNiagaraFunctionLibrary::OverrideSystemUserVariableStaticMeshComponent(Effect, Parameter, WeaponMesh);
			break;
		case EVBWeaponShapeSource::BodyBones:
			// 주먹 무기: 보이는 메시의 손, 아래팔 뼈만.
			VB_CLOG(!ShapeBody.IsValid(), Warning, "SummonedWeapon %s: 모양을 따올 몸이 없다 - 주먹 소환 입자가 모양 없이 난다", *GetName());
			if (USkeletalMeshComponent* Body = ShapeBody.Get())
			{
				UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent(Effect, Parameter, Body);
				UNiagaraFunctionLibrary::SetSkeletalMeshDataInterfaceFilteredBones(Effect, Parameter, VisualPart.ShapeBones);
			}
			break;
		default:
			break;
		}
	}
}

void AVBSummonedWeapon::SetManifestProgress(float Progress)
{
	const float PreviousProgress = ManifestProgress;
	ManifestProgress = FMath::Clamp(Progress, 0.0f, 1.0f);
	WeaponMesh->SetVisibility(ManifestProgress > 0.0f && VisualPart.Mesh != nullptr);
	if (ManifestMaterial)
	{
		VBWeaponManifest::SetProgress(*ManifestMaterial, ManifestProgress);
	}
	// 재질의 경계와 같은 로컬 투영값. 손이 움직여도 두 표현이 벌어지지 않는다.
	const float Front = FMath::Lerp(Reveal.Min, Reveal.Max, ManifestProgress * VisualPart.RevealFrontScale - VisualPart.RevealFrontBias);
	FormationFX->SetRelativeLocation(Reveal.BoundsCenter + Reveal.Axis * (Front - FVector::DotProduct(Reveal.BoundsCenter, Reveal.Axis)));
	const bool bForming = ManifestProgress > 0.001f && ManifestProgress < 0.999f;
	if (bForming && VisualPart.FormationEffect)
	{
		FormationFX->SetVariableLinearColor(NiagaraColorParameter, VisualPart.Color * VisualPart.FormationColorScale);
		FormationFX->SetVariableLinearColor(NiagaraSecondaryColorParameter, VisualPart.Color * VisualPart.FormationSecondaryColorScale);
		if (!FormationFX->IsActive())
		{
			FormationFX->Activate(true);
		}
	}
	else if (FormationFX->IsActive())
	{
		FormationFX->Deactivate();
	}
	// 검은 미완성 부위에 오라가 먼저 나타나지 않는다. 맨손은 오라 자체가 차오른다.
	const bool bIdleVisible = VisualPart.Mesh ? ManifestProgress >= 0.999f : ManifestProgress > 0.001f;
	if (bIdleVisible && VisualPart.IdleEffect)
	{
		// 손 외피의 단발 입자는 생성 순간 색을 보관한다. 밝기는 처음부터 목표값으로,
		// 실체화는 아래 컴포넌트 크기로 표현해야 거의 검은 외피가 고정되지 않는다.
		const float Strength = VisualPart.Mesh ? VisualPart.IdleColorStrengthWithMesh : VisualPart.IdleColorStrengthBareHand;
		IdleFX->SetVariableLinearColor(NiagaraColorParameter, VisualPart.Color * Strength);
		IdleFX->SetVariableLinearColor(NiagaraSecondaryColorParameter, VisualPart.Color * Strength * VisualPart.IdleSecondaryColorScale);
		IdleFX->SetRelativeScale3D(VisualPart.IdleScale * (VisualPart.Mesh ? 1.0f : FMath::Lerp(0.2f, 1.0f, ManifestProgress)));
		// 경계 시스템의 단발 외피는 생성 시 크기도 캡처한다. 완성 순간 한 번만
		// 다시 초기화해 최종 손 크기를 보장한다(진행 중 반복 재시작하지 않는다).
		const bool bHandCompleted = !VisualPart.Mesh && PreviousProgress < 0.999f && ManifestProgress >= 0.999f;
		if (!IdleFX->IsActive() || bHandCompleted)
		{
			IdleFX->ReinitializeSystem();
		}
	}
	else if (IdleFX->IsActive())
	{
		IdleFX->DeactivateImmediate();
	}

	if (ManifestProgress <= 0.0f)
	{
		FormationFX->DeactivateImmediate();
	}
}

void AVBSummonedWeapon::PreviewSummon()
{
	RebuildVisual();
	PreviewElapsed = 0.0f;
	SetActorTickEnabled(true);
}

void AVBSummonedWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PreviewElapsed >= 0.0f)
	{
		PreviewElapsed += DeltaSeconds;
		SetManifestProgress(PreviewElapsed / FMath::Max(VisualPart.PreviewDuration, KINDA_SMALL_NUMBER));
		if (ManifestProgress >= 1.0f)
		{
			PreviewElapsed = -1.0f;
			SetActorTickEnabled(false);
		}
	}
}
