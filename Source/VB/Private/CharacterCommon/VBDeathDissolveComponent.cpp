// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "CharacterCommon/VBDeathDissolveComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/VBDeathDissolveConfig.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Data/VBDeathDissolveSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "TimerManager.h"
#include "VB/VB.h"


namespace
{
	// 파편이 올라온 뒤 메시를 숨길 때까지 기다리는 프레임 수. 가루가 표면을 덮어 늦는 쪽 오차는 안 보이므로 넉넉히 잡는다.
	constexpr int32 GHideMeshAfterFrames = 3;

	// 파편 재질이 기본색을 받는 텍스처 파라미터 이름. M_VB_DeathDust 의 것과 같아야 한다.
	const FName GParamDebrisBaseTexture(TEXT("BaseTexture"));

	// 즉시 시작으로 볼 지연 하한(초). rate 0 이하 SetTimer 는 clear 로 처리돼 콜백이 안 오므로 직접 부른다.
	constexpr float GImmediateStartEpsilon = 0.01f;
}


UVBDeathDissolveComponent::UVBDeathDissolveComponent()
{
	// 진행은 GPU 가 센다. CPU 는 시작 순간에만 일한다.
	PrimaryComponentTick.bCanEverTick = false;
}

USkeletalMeshComponent* UVBDeathDissolveComponent::ResolveTargetMesh() const
{
	// 소유자를 ACharacter 로만 본다 - 플레이어와 적은 공통 VB 베이스가 없다.
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	return OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
}

UVBDeathDissolveConfig* UVBDeathDissolveComponent::ResolveConfig(UVBDeathDissolveConfig* Explicit)
{
	// 판단은 UVBDeathDissolveSettings::ResolveConfig 한 곳에 있다. 이 함수는 위임만 한다.
	return UVBDeathDissolveSettings::ResolveConfig(Explicit);
}

void UVBDeathDissolveComponent::ScheduleDissolve(float SecondsUntilCleanup, UVBDeathDissolveConfig* Config)
{
	Config = UVBDeathDissolveSettings::ResolveConfig(Config);

	// 설정이 없거나 길이가 0 이하면 이 연출은 꺼진 것이다. 길이 하나가 켜짐 여부를 겸한다.
	if (!Config || Config->Duration <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 정리 시각이 이미 지났거나 같으면 시작할 창이 없다. 억지로 시작하면 한 프레임 깜빡이고 사라진다.
	if (SecondsUntilCleanup <= GImmediateStartEpsilon)
	{
		VB_LOG(Warning, "사망 디졸브: %s 정리까지 %.2f초뿐이라 연출을 건너뛴다. DeathCleanupDelay 를 늘려라.",
			*GetNameSafe(GetOwner()), SecondsUntilCleanup);
		return;
	}

	// 길이가 남은 시간보다 크면 줄이고 경고한다 - 정리 지연과 디졸브 길이 두 자산이 어긋났다는 신호다.
	PendingDuration = Config->Duration;
	if (PendingDuration > SecondsUntilCleanup)
	{
		VB_LOG(Warning, "사망 디졸브: %s 길이 %.2f초가 남은 정리 시간 %.2f초보다 길다 - 남은 시간으로 줄인다.",
			*GetNameSafe(GetOwner()), PendingDuration, SecondsUntilCleanup);
		PendingDuration = SecondsUntilCleanup;
	}

	PendingConfig = Config;

	// 끝나는 순간이 정리 시각과 맞도록 시작을 역산한다. 제거 시각은 호출자 타이머 하나가 관리한다(여기서 Destroy 하지 않는다).
	const float StartDelay = SecondsUntilCleanup - PendingDuration;
	if (StartDelay <= GImmediateStartEpsilon)
	{
		BeginDissolve();
		return;
	}

	World->GetTimerManager().SetTimer(StartTimerHandle, this, &UVBDeathDissolveComponent::BeginDissolve, StartDelay, false);
}

void UVBDeathDissolveComponent::ResetDissolve()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StartTimerHandle);
	}

	// 아직 안 터진 숨기기 예약을 취소한다(안 하면 되돌린 직후 메시를 다시 숨긴다).
	if (HideTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HideTickerHandle);
		HideTickerHandle.Reset();
	}

	// 숨긴 메시를 다시 보이게 한다. 이것을 빠뜨리면 되돌렸는데 메시가 안 돌아온다.
	if (USkeletalMeshComponent* Mesh = HiddenMesh.Get())
	{
		Mesh->SetVisibility(true, /*bPropagateToChildren*/ true);
	}

	HiddenMesh.Reset();

	// 아직 날아가는 파편도 거둔다(안 거두면 직전 재생과 섞여 보인다). 실제 사망 경로는 여기 오지 않는다.
	if (UNiagaraComponent* Debris = SpawnedDebris.Get())
	{
		Debris->DestroyComponent();
	}

	SpawnedDebris.Reset();

	PendingConfig = nullptr;
	PendingDuration = 0.0f;
}

void UVBDeathDissolveComponent::BeginDissolve()
{
	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh();
	if (!TargetMesh || !PendingConfig)
	{
		return;
	}

	// 슬롯 머티리얼은 건드리지 않는다 - 같은 프레임에 메시를 숨기므로 MID 디졸브 값은 보일 틈이 없다.

	// 시작 로그. 파편이 안 보일 때 예약, 설정, 호출 경로를 지워 주는 진단 기준점이다.
	VB_LOG(Log, "사망 디졸브 시작: %s 메시 %s, 길이 %.2f초",
		*GetNameSafe(GetOwner()), *GetNameSafe(TargetMesh->GetSkeletalMeshAsset()), PendingDuration);

	SpawnDebris(TargetMesh);

	// 원본 메시를 숨긴다. 깎기는 몬스터마다 머티리얼 작업이 필요해 메시 복제 파티클 + 숨기기로 한다.
	// 숨기기는 몇 프레임 늦게 한다 - 가루가 표면을 덮어 늦는 쪽 오차는 안 보이고, 이른 쪽만 구멍이 된다.
	// 월드 타이머는 에디터 월드에서 안 돌아서 코어 티커(FTSTicker)를 쓴다.
	HiddenMesh = TargetMesh;

	// 앞선 예약이 남아 있으면 먼저 거둔다(핸들이 덮이면 ResetDissolve 가 옛 예약을 취소할 수 없다).
	if (HideTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HideTickerHandle);
	}

	int32 FramesLeft = GHideMeshAfterFrames;
	HideTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
		[this, FramesLeft](float) mutable -> bool
		{
			if (--FramesLeft > 0)
			{
				return true; // 아직 기다린다
			}

			if (USkeletalMeshComponent* Mesh = HiddenMesh.Get())
			{
				Mesh->SetVisibility(false, /*bPropagateToChildren*/ true);
			}

			return false; // 한 번 하고 끝
		}));
}

UTexture* UVBDeathDissolveComponent::ResolveDebrisTexture(const USkeletalMeshComponent* TargetMesh)
{
	if (!TargetMesh)
	{
		return nullptr;
	}

	// 삼각형이 가장 많은 섹션의 재질을 고른다. 그 이유는 선언부 주석에 있다.
	int32 BestMaterialIndex = 0;
	if (const USkeletalMesh* Asset = TargetMesh->GetSkeletalMeshAsset())
	{
		if (const FSkeletalMeshRenderData* RenderData = Asset->GetResourceForRendering())
		{
			if (RenderData->LODRenderData.Num() > 0)
			{
				uint32 BestTriangles = 0;
				for (const FSkelMeshRenderSection& Section : RenderData->LODRenderData[0].RenderSections)
				{
					if (Section.NumTriangles > BestTriangles)
					{
						BestTriangles = Section.NumTriangles;
						BestMaterialIndex = Section.MaterialIndex;
					}
				}
			}
		}
	}

	UMaterialInterface* Material = TargetMesh->GetMaterial(BestMaterialIndex);
	if (!Material)
	{
		return nullptr;
	}

	// 그 재질이 쓰는 텍스처를 훑는다(파라미터 이름은 머티리얼마다 없을 수 있어 안 쓴다).
	// 5인자 GetUsedTextures 는 5.7 에서 본문이 비어 빈 배열을 조용히 준다 - 쓰지 않는다.
	TArray<UTexture*> Used;
	Material->GetUsedTextures(Used);

	// 기본색을 고른다. 노멀맵은 거르고, 이름이 기본색을 뜻하는 것을 먼저, 없으면 첫 2D 텍스처를 쓴다.
	UTexture2D* Fallback = nullptr;
	for (UTexture* Texture : Used)
	{
		UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		if (!Texture2D || Texture2D->CompressionSettings == TC_Normalmap)
		{
			continue;
		}

		const FString Name = Texture2D->GetName();
		if (Name.EndsWith(TEXT("_D")) || Name.Contains(TEXT("BaseColor")) ||
			Name.Contains(TEXT("Albedo")) || Name.Contains(TEXT("Diffuse")))
		{
			return Texture2D;
		}

		if (!Fallback)
		{
			Fallback = Texture2D;
		}
	}

	return Fallback;
}

void UVBDeathDissolveComponent::SpawnDebris(USkeletalMeshComponent* TargetMesh)
{
	if (!PendingConfig || !PendingConfig->DebrisEffect || !TargetMesh)
	{
		return;
	}

	// 월드에 띄운다. 붙이면 Outer 가 적 액터가 되어 적 Destroy 때 파편도 같이 파괴된다(Detach 로는 안 바뀐다).
	// 자동 활성화를 끄고 스폰한다 - 메시를 꽂기 전에 버스트가 터지면 레퍼런스 자세를 본다. 만들고 -> 꽂고 -> 켠다.
	UNiagaraComponent* Debris = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		TargetMesh,
		PendingConfig->DebrisEffect,
		TargetMesh->GetComponentLocation(),
		TargetMesh->GetComponentRotation(),
		TargetMesh->GetComponentScale(),
		/*bAutoDestroy*/ true,
		/*bAutoActivate*/ false);
	if (!Debris)
	{
		VB_LOG(Warning, "사망 디졸브: %s 파편 시스템 %s 스폰 실패.",
			*GetNameSafe(GetOwner()), *GetNameSafe(PendingConfig->DebrisEffect));
		return;
	}

	// 지켜보기만 한다 - 시험 재생을 물릴 때 거두려는 것이고, 살려 두려는 것이 아니다.
	SpawnedDebris = Debris;

	// 파티클이 지금 이 자세의 표면에서 태어나도록 런타임에 메시를 건넨다. 이름이 어긋나면 무동작이라 로그를 남긴다.
	if (!PendingConfig->DebrisMeshParameterName.IsNone())
	{
		UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent(
			Debris, PendingConfig->DebrisMeshParameterName.ToString(), TargetMesh);
		VB_LOG(Log, "사망 디졸브: %s 파편에 메시를 꽂았다 (파라미터 '%s').",
			*GetNameSafe(GetOwner()), *PendingConfig->DebrisMeshParameterName.ToString());
	}

	// 파편 색을 죽는 메시에서 직접 읽는다(설정에 몬스터별 텍스처를 두면 빠뜨려도 조용하다).
	// 실패할 자리 둘 다 경고한다.
	if (!PendingConfig->DebrisMaterial)
	{
		VB_LOG(Warning, "사망 디졸브: %s 설정(%s)에 DebrisMaterial 이 없다 - 파편이 Niagara 기본 재질 색으로 난다.",
			*GetNameSafe(GetOwner()), *GetNameSafe(PendingConfig));
	}
	else if (UTexture* BaseTexture = ResolveDebrisTexture(TargetMesh))
	{
		UMaterialInstanceDynamic* DustMID = UMaterialInstanceDynamic::Create(PendingConfig->DebrisMaterial, this);
		if (DustMID)
		{
			DustMID->SetTextureParameterValue(GParamDebrisBaseTexture, BaseTexture);
			Debris->SetVariableMaterial(PendingConfig->DebrisMaterialParameterName, DustMID);
			VB_LOG(Log, "사망 디졸브: %s 파편 색을 %s 에서 가져왔다.",
				*GetNameSafe(GetOwner()), *BaseTexture->GetName());
		}
	}
	else
	{
		VB_LOG(Warning, "사망 디졸브: %s 메시에서 기본색 텍스처를 못 찾았다 - 파편이 재질 기본 색으로 난다.",
			*GetNameSafe(GetOwner()));
	}

	// 메시가 들어간 뒤에 켠다. 이 줄이 위로 올라가면 다시 레퍼런스 자세에서 터진다.
	Debris->Activate();

	// 켠 그 자리에서 한 틱을 돌린다. Activate 만으로는 파티클이 없어 같은 프레임 메시 숨기기와 겹쳐 깜빡인다.
	// 대가는 파편이 한 틱 미리 늙는 것뿐이다. 델타가 0 이면 아무 일도 안 나므로 경고한다.
	const UWorld* DebrisWorld = GetWorld();
	const float TickDelta = DebrisWorld ? DebrisWorld->GetDeltaSeconds() : 0.0f;
	if (TickDelta > 0.0f)
	{
		Debris->AdvanceSimulation(1, TickDelta);
	}
	else
	{
		VB_LOG(Warning, "사망 디졸브: %s 월드 델타가 0 이라 파편 첫 틱을 못 돌렸다 - 한 프레임 깜빡일 수 있다.",
			*GetNameSafe(GetOwner()));
	}
}
