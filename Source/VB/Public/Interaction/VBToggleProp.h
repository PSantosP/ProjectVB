// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBToggleProp.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class UVBInteractableComponent;

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

// 선택한 소품 애니와 서버 시작 시각을 같이 보낸다. 발차기/뒷면 동작도 원격에서 같은 것을 재생한다.
USTRUCT()
struct FVBPropPlayback
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<UAnimSequence> Animation;
	UPROPERTY()
	double ServerStartTime = 0.0;
	UPROPERTY()
	float StartPosition = 0.0f;
	UPROPERTY()
	float PlayRate = 1.0f;
	UPROPERTY()
	bool bSnapToEnd = true;
};

// 대리 메시 한 조각. 메시가 없는 팩 소품(밸브, 게이트 등)을 기본 도형으로 대신한다.
// 레벨 인스턴스에서 바로 고칠 수 있게 BP 컴포넌트가 아니라 데이터로 둔다.
USTRUCT(BlueprintType)
struct FVBPropProxyPart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UStaticMesh> Mesh;

	// 소품 루트 기준, 닫힌 자세(소품 애니 0초)에서의 자리.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	FTransform Transform;

	// 비어 있으면 고정. 이름이 있으면 소품 애니의 그 뼈를 따라 움직인다(닫힌 자세에서 뼈와의 차이를 유지).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	FName FollowBone;

	// 비어 있으면 메시 자기 재질. 조각 컴포넌트가 Transient 라 재질도 데이터가 갖는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UMaterialInterface> Material;

	// 켜면 보이기만 한다(충돌 없음). PropMesh 의 피직스 바디가 길을 막을 때, 그 위에 다른 모양을 씌우는 조각에 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	bool bVisualOnly = false;
};

/**
 * 켜짐/꺼짐 두 상태를 오가는 소품(문, 스위치, 밸브, 게이트, 찬장). 상태는 Interactable 컴포넌트가 관리하고 여기는 보여 주기만 한다.
 * PropMesh 가 소품 애니를 직접 재생하거나, 없으면 ProxyParts 가 애니 뼈를 따라간다. LinkedProps 는 같은 상태로 맞춘다.
 * 상태 변경, 연결은 서버에서만, 애니는 재생 정보와 서버 시각을 복제해 맞춘다.
 */
UCLASS()
class VB_API AVBToggleProp : public AActor
{
	GENERATED_BODY()

public:
	AVBToggleProp();

	// 실제 표시 중인 소품의 뼈/소켓. 대리 메시도 같은 소품 애니의 현재 시각을 사용한다.
	bool GetInteractionBoneTransform(FName BoneName, FTransform& OutWorldTransform) const;
	void SetInteractionPlayRate(float Rate);

	// 설정이 길을 막는다고 하면 이 배치가 지킬 수 있는가. 빈 문자열 = 문제 없음.
	FString ValidateSetup() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<USkeletalMeshComponent> PropMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UVBInteractableComponent> Interactable;

	// 대리 메시 조각들. PropMesh 에 스켈레탈 메시가 있으면 보통 비워 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TArray<FVBPropProxyPart> ProxyParts;

	// 켜면 PropMesh 는 안 보이고(피직스 바디와 애니는 그대로) ProxyParts 가 보이는 모양을 맡는다. 문짝처럼 뼈 하나를 따라가는 교체용.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	bool bProxyReplacesPropMesh = false;

	virtual void OnConstruction(const FTransform& Transform) override;

	// 켜져 있을 때 프롬프트에 나올 이름(「닫기」「끄기」). 꺼져 있을 때는 설정의 ActionText.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	FText DeactivateActionText;

	// 한 번 켜면 끝인가. 그러면 켜진 뒤 상호작용이 꺼진다 - 다 쓰였을 때(IsOneShotSpent)만.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	bool bOneShot = false;

	// 일회용이 다 쓰였나. 바탕은 늘 참, 서랍장(AVBLootContainer)은 남은 몫이 없을 때로 덮는다.
	// 잠그는 쪽은 이 소품 하나다(RefreshOneShotLock) - 파생은 판단만 덮는다.
	virtual bool IsOneShotSpent() const { return true; }

	// 서버에서만. 일회용이고 켜져 있고 다 쓰였으면 상호작용을 끈다. 끄기만 한다(배치 때 잠근 대상을 풀지 않게).
	void RefreshOneShotLock();

	// 이 소품과 함께 켜지고 꺼지는 소품들(스위치 -> 문, 게이트).
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Interaction")
	TArray<TObjectPtr<AVBToggleProp>> LinkedProps;

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UFUNCTION()
	void HandleActivationChanged(bool bActivated, bool bInstant);
	// 상호작용의 완료(효과까지)가 끝났다 - 일회용은 여기서 잠근다. 완료가 실패하면 다시 시도할 수 있게 남는다.
	UFUNCTION()
	void HandleOneShotCompletion(AActor* InteractingActor);

	// 서버: 상태에 딸린 것들(프롬프트 이름, 일회용, 연결)을 맞춘다. bInstant 면 연결 소품도 애니 없이.
	void ApplyServerSideEffects(bool bActivated, bool bInstant);

	// 대리 메시 조각을 (다시) 짓는다.
	void BuildProxyParts();

	// 길을 막는 종류면 문짝 메시의 피직스 바디가 질의로 Pawn 을 막게 한다(엔진 기본값은 충돌 없음).
	void ApplyPassageCollision();

	// 소품 애니를 튼다. bSnapToEnd 면 재생 없이 끝 자세로 맞춘다(시작 상태).
	void PlayPropAnimation(UAnimSequence* Animation, bool bSnapToEnd, float StartPosition = 0.0f);

	UPROPERTY(ReplicatedUsing=OnRep_Playback)
	FVBPropPlayback Playback;

	UFUNCTION()
	void OnRep_Playback();

	// 뼈 따라가기 - 대리 메시 갈래.
	void ApplyBoneParts(float Time);
	FTransform SampleBoneComponentSpace(FName BoneName, float Time) const;

	// Interactor에 보관한 동작 우선, 없으면 앞뒤 판정. 사람이 없으면(연결) 앞쪽.
	UAnimSequence* ResolvePropAnimation(bool bActivated, const AActor* Interactor) const;

	// 대리 메시 갈래의 재생 상태.
	UPROPERTY()
	TObjectPtr<UAnimSequence> BonePartAnimation;
	float BonePartTime = 0.0f;

	// OnConstruction 이 만든 조각 컴포넌트. 다시 지을 때 지우려고 들고 있다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ProxyComponents;

	// 뼈를 따라가는 조각 -> (컴포넌트, 닫힌 자세에서 뼈에 대한 상대 변환)
	struct FResolvedPart
	{
		TWeakObjectPtr<USceneComponent> Component;
		FName BoneName;
		FTransform BoneToComponent;
	};
	TArray<FResolvedPart> ResolvedParts;

	// 서버에서 확정 순간 선택된 동작을 읽는 용도. 원격 표시는 Playback으로 전달한다.
	TWeakObjectPtr<AActor> LastInteractor;
};
