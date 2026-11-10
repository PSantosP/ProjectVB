#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Character/Appearance/VBBodyFade.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "UObject/PrimaryAssetId.h"
#include "VBCharacterAppearanceComponent.generated.h"

class AController;
class APawn;
class UChildActorComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UVBAppearancePiece;
class UVBCharacterAppearance;
class UVBEquipmentComponent;
class UVBSaveParticipantSubsystem;
struct FStreamableHandle;

class UVBCharacterAppearanceComponent;
DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnAppearanceChanged, UVBCharacterAppearanceComponent* /*Appearance*/);

// UVBCharacterAppearanceComponent - 본체 Mesh 아래 붙인 자식 액터가 리타겟으로 본체 자세를 따라 하는 플레이어 겉모습이다
// 애니메이션, 판정, 몽타주는 늘 본체가 하고, 리타겟 노드가 부착 사슬로 원본 메시를 찾기에 본체 아래 붙인다
// authority 가 AppearanceId 와 EquippedPieces 를 쓰고, 각 기계가 자식을 로컬로 만들며 세대가 다른 로드 응답은 버린다
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBCharacterAppearanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBCharacterAppearanceComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 장비 출처는 이 컴포넌트가 직접 연결한다: 소유 Pawn 의 컨트롤러 변경을 구독하고(서버) 그 PlayerState 의 장비를 출처로 삼는다.
	//   등록 때 지금 상태로 한 번 맞추고(엔진은 지난 통지를 다시 주지 않는다), 등록을 풀면 구독을 모두 푼다.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 서버. 무효 ID 는 본체 그대로다. 목록(에셋 매니저 VBAppearance)에 없거나 평상시가 아니면 거절하고 false.
	bool SetAppearance(const FPrimaryAssetId& NewId);
	// authority 가 정하는 출처다 - 지금 상태를 바로 반영하고, 바뀌면 구독 둘을 모두 푼다
	void SetEquipmentSource(UVBEquipmentComponent* Source);

	// 서버 판정. 무엇이 막는지는 VBBodyControlPolicy::BlocksAppearanceSwap 이 정한다 - 여기는 사실만 모은다.
	bool CanChangeAppearance() const;

	UFUNCTION(Server, Reliable)
	void ServerRequestAppearance(FPrimaryAssetId NewId);

	const FPrimaryAssetId& GetAppearanceId() const { return AppearanceId; }
	bool IsVisualActive() const { return PrimaryVisualMesh.IsValid(); }

	// 보이는 주 바디 메시. 겉모습이 없으면 본체.
	USkeletalMeshComponent* GetPrimaryVisualMesh() const;

	// 현재 보이는 바디와 의상 메시를 모으며 숨겨진 본체는 제외한다.
	static void GatherVisibleMeshes(const AActor* Owner, TArray<USkeletalMeshComponent*>& OutMeshes);

	// 표시 재질을 잠시 빌리며 요청 번호가 일치할 때만 복원한다.
	uint64 BeginBodyFade(const UVBBodyFadeConfig* Config, float Duration);
	void EndBodyFade(uint64 Request);
	bool IsBodyFadeActive() const { return BodyFade.IsActive(); }

	// 본체 소켓에 붙일 보이는 것의 부착 대상. 겉모습 주 바디에 대응 소켓이 없으면 본체와 그 소켓을 준다(경고).
	void ResolveAttachTarget(FName DriverSocket, USceneComponent*& OutParent, FName& OutSocket) const;

	// 표시가 바뀐 뒤(겉모습 준비 완료 또는 본체로 복귀). 무기 재부착 등 보이는 것의 소비처가 받는다.
	FVBOnAppearanceChanged OnAppearanceChanged;

	// 로드가 끝난 조각 메시를 한 번에 바디에 끼우고 덮인 신체 부위를 숨긴다. 이전 조각은 먼저 제거한다
	// 스켈레톤이 바디와 다른 조각은 리더 포즈를 못 따르므로 끼우지 않고 경고한다. 반환 = 끼운 조각 수
	static int32 ApplyPieces(USkeletalMeshComponent& Body, FName CharacterId, const TArray<const UVBAppearancePiece*>& Pieces,
		TArray<TObjectPtr<USkeletalMeshComponent>>& InOutPieceComponents);

private:
	UPROPERTY(Transient)
	FVBBodyFade BodyFade;

	friend struct FVBEquipmentAppearanceTestAccess;
	UFUNCTION()
	void HandlePawnControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);
	// 지금 폰의 지금 PlayerState 로 출처를 맞춘다(서버, 게임 월드). 빙의가 풀린 폰이면 PlayerState 가 없어 출처도 끊긴다.
	void SyncEquipmentSourceFromPawn();
	void UnbindEquipmentSource();
	void RefreshEquippedPieces();
	void HandleEquipmentSaveFinalized();
	void ApplyEquippedPieces();
	UFUNCTION()
	void OnRep_EquippedPieces();

	UPROPERTY(ReplicatedUsing=OnRep_EquippedPieces)
	TArray<TSoftObjectPtr<UVBAppearancePiece>> EquippedPieces;
	TWeakObjectPtr<UVBEquipmentComponent> EquipmentSource;
	TWeakObjectPtr<UVBSaveParticipantSubsystem> EquipmentSaveRegistry;
	FDelegateHandle EquipmentChangedHandle;
	FDelegateHandle EquipmentSaveFinalizedHandle;
	bool bEquipmentRefreshPending = false;

	UFUNCTION()
	void OnRep_Appearance();

	void ApplyAppearance();
	void OnDefinitionLoaded(int32 Generation, FSoftObjectPath DefinitionPath);
	void OnVisualClassLoaded(int32 Generation);
	bool AttachVisual(UClass* VisualClass);
	void ClearVisual();
	void SetDriverHidden(bool bHidden);
	USkeletalMeshComponent* GetDriverMesh() const;

	// 기본, 장착 DA 를 slot 과 CharacterId 로 골라 메시를 로드한 뒤 준비된 묶음으로 바꾼다
	// 조각 세대 번호가 따로 있어 장비 변경, 겉모습 교체, EndPlay 뒤의 낡은 응답을 버린다
	void ComposePieces();
	void OnPiecesLoaded(int32 Generation);
	void OnPieceMeshesLoaded(int32 Generation);

	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	FPrimaryAssetId AppearanceId;

	UPROPERTY(Transient)
	TObjectPtr<UChildActorComponent> VisualChild;

	UPROPERTY(Transient)
	TObjectPtr<UVBCharacterAppearance> ActiveDefinition;

	UPROPERTY(Transient)
	TObjectPtr<UVBCharacterAppearance> PendingDefinition;

	TWeakObjectPtr<USkeletalMeshComponent> PrimaryVisualMesh;
	TWeakObjectPtr<USkeletalMeshComponent> HiddenDriver;
	TSharedPtr<FStreamableHandle> DefinitionHandle;
	TSharedPtr<FStreamableHandle> ClassHandle;
	int32 RequestGeneration = 0;
	bool bDriverWasVisible = true;

	// 읽는 중인 조각(메시가 오기 전까지 살려 둔다)과 끼운 조각 컴포넌트(겉모습 액터 소유 - 겉모습이 바뀌면 함께 사라진다).
	UPROPERTY(Transient)
	TArray<TObjectPtr<const UVBAppearancePiece>> PendingPieces;
	UPROPERTY(Transient)
	TArray<TObjectPtr<const UVBAppearancePiece>> PendingDefaultPieces;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> PieceComponents;

	TSharedPtr<FStreamableHandle> PieceHandle;
	int32 PieceGeneration = 0;
};

/**
 * 부른 플레이어의 겉모습을 바꾼다. 영역 진입, 대화, 퀘스트 보상에서 쓴다.
 * 바꾸는 길은 UVBCharacterAppearanceComponent::SetAppearance 하나다 - 목록 확인과 평상시 판정도 그쪽이 한다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="겉모습 바꾸기"))
struct VB_API FVBEffect_SetAppearance : public FVBEffect
{
	GENERATED_BODY()

	// 겉모습 자산 ID. 비우면 본체로 돌아간다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(AllowedTypes="VBAppearance"))
	FPrimaryAssetId Appearance;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
