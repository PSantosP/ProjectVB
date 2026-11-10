// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "VBFastTravelComponent.generated.h"

class AVBSavePoint;
class UVBPlayerRelocationComponent;

/** 목적지 한 줄. 서버가 정한 것 그대로다(이름은 지점 배치에서, 지역은 지점의 RegionId). */
USTRUCT()
struct VB_API FVBFastTravelEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag PointId;

	UPROPERTY()
	FText DisplayName;

	UPROPERTY()
	FGameplayTag RegionId;
};

/** 소유 클라이언트가 그릴 현재 목적지 목록. 열려 있지 않으면 IsActive() 가 거짓이다. */
USTRUCT()
struct VB_API FVBFastTravelView
{
	GENERATED_BODY()

	// 연 저장점. 비면 닫힌 상태.
	UPROPERTY()
	FGameplayTag FromPointId;

	UPROPERTY()
	TArray<FVBFastTravelEntry> Entries;

	bool IsActive() const { return FromPointId.IsValid(); }
};

/** 이동 판정의 답. 화면은 이것으로 버튼을 켜고 끄고 까닭을 보인다. */
UENUM(BlueprintType)
enum class EVBFastTravelResult : uint8
{
	Ok,
	// 열린 목록이 없거나 연 저장점과 다르다.
	NotOpen,
	// 목적지가 발견되지 않았거나 이 월드에 없다.
	UnknownDestination,
	// 지금 서 있는 저장점이다.
	SamePoint,
	// 폰이 없거나 죽었거나 연 저장점에서 멀어졌다.
	NotAtPoint,
	InCombat,
	AlreadyTravelling,
};

DECLARE_MULTICAST_DELEGATE(FVBOnFastTravelViewChanged);

/**
 * 빠른 이동 목록을 관리한다. AVBPlayerController 에 붙고, 저장점 상호작용이 서버에서 연다. 화면은 OnViewChanged 로만 알린다.
 * 서버 판정을 통과한 이동은 UVBPlayerRelocationComponent 에 맡기고, 옮기기가 시작되면 열린 목록을 닫는다.
 * 원격 소유 클라이언트의 UVBTravelPointSubsystem 에 서버 발견 목록 사본을 전달한다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBFastTravelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBFastTravelComponent();

	// 서버에서만. 이미 열려 있거나 이동 중이거나 지점이 등록되지 않았으면 false.
	bool OpenTravel(AVBSavePoint* FromPoint);

	// 서버에서만.
	void CloseTravel();

	bool IsTravelOpen() const { return ServerFrom.IsValid(); }
	// 이 플레이어를 옮기는 중이면 true. 사실은 UVBPlayerRelocationComponent 가 갖는다.
	bool IsTravelling() const;

	// 표시가 부른다. 판단은 서버가 다시 한다.
	void RequestTravel(const FGameplayTag& Destination);
	void RequestClose();

	const FVBFastTravelView& GetView() const { return View; }
	FVBOnFastTravelViewChanged OnViewChanged;

	// 화면의 버튼이 쓴다(표시용). 보기에 있는 목적지인지와 같은 지점인지만 본다 - 전투, 거리는 서버가 본다.
	EVBFastTravelResult PreviewTravel(const FGameplayTag& Destination) const;

	static const TCHAR* LexToString(EVBFastTravelResult Result);

protected:
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 화면이 어두워지는 시간(초). 이 시간이 지난 뒤에 옮긴다 - 옮기는 순간이 보이지 않게.
	UPROPERTY(EditDefaultsOnly, Category="VB|FastTravel", meta=(ClampMin="0"))
	float FadeOutSeconds = 0.4f;

	// 도착 후 밝아지는 시간(초).
	UPROPERTY(EditDefaultsOnly, Category="VB|FastTravel", meta=(ClampMin="0"))
	float FadeInSeconds = 0.6f;

	// 연 저장점의 이동 상호작용 범위에 더하는 여유(cm). 이 거리를 넘으면 이동을 거절한다(목록을 연 채 걸어가 다른 곳에서 쓰지 못하게).
	UPROPERTY(EditDefaultsOnly, Category="VB|FastTravel", meta=(ClampMin="0"))
	float DistanceSlack = 200.0f;

private:
	friend class FVBFastTravelTestAccess;

	UFUNCTION(Server, Reliable)
	void ServerTravel(const FGameplayTag& FromPointId, const FGameplayTag& Destination);

	UFUNCTION(Server, Reliable)
	void ServerClose();

	UFUNCTION(Client, Reliable)
	void ClientShowTravel(const FVBFastTravelView& InView);

	UFUNCTION(Client, Reliable)
	void ClientCloseTravel();

	// 소유 클라이언트 -> 서버: 현재 발견 목록을 보내 달라.
	UFUNCTION(Server, Reliable)
	void ServerRequestDiscovered();

	// 서버 -> 소유 클라이언트: 발견 목록 전체. 권한 쪽(호스트, 싱글)에서는 버린다 - 서버 목록이 원본이다.
	UFUNCTION(Client, Reliable)
	void ClientSetDiscovered(const TArray<FGameplayTag>& ServerDiscovered);

	// 서버: 원격 소유 클라이언트에 현재 목록을 보낸다.
	void SendDiscovered();

	// 서버: 판정 하나. 서버 이동과 시험이 같은 것을 쓴다.
	EVBFastTravelResult CheckTravel(const FGameplayTag& FromPointId, const FGameplayTag& Destination) const;

	void SendView();
	APawn* GetPawn() const;
	UVBPlayerRelocationComponent* FindRelocation() const;

	TWeakObjectPtr<AVBSavePoint> ServerFrom;

	// 서버에서 원격 소유자의 PC 만 발견 변경에 바인딩한다. 호스트 자신의 PC 는 서버 목록을 바로 읽으므로 바인딩하지 않는다.
	FDelegateHandle DiscoveredChangedHandle;

	// 소유 클라이언트가 그릴 데이터. 리슨 서버 호스트에서는 서버 상태와 한 객체에 같이 있다.
	UPROPERTY()
	FVBFastTravelView View;
};
