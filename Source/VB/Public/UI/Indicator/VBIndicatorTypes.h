// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VBIndicatorTypes.generated.h"

class AActor;
class APlayerController;
class USceneComponent;
class UVBIndicatorRegistrySubsystem;

UENUM()
enum class EVBIndicatorViewerScope : uint8
{
	AllLocalViewers,
	OneViewer
};

UENUM()
enum class EVBIndicatorSortOrder : uint8
{
	Nearest,
	PriorityThenRegistration
};

// 후보의 수명과 보이기는 따로다. 아래 참조는 모두 약참조라 대상을 붙잡지 않는다.
USTRUCT()
struct VB_API FVBIndicatorRequest
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag Group;

	UPROPERTY()
	TWeakObjectPtr<USceneComponent> AnchorComponent;

	UPROPERTY()
	FName AnchorSocket = NAME_None;

	// 앵커, 소켓 위치를 구한 뒤 월드 공간에서 더한다. 화면 투영은 층이 한다.
	UPROPERTY()
	FVector WorldOffset = FVector::ZeroVector;

	// 점유 표시, 죽음 판정에 쓰는 대상 액터. 앵커 컴포넌트의 Owner 가 아니어도 된다.
	UPROPERTY()
	TWeakObjectPtr<AActor> Subject;

	UPROPERTY()
	TWeakObjectPtr<UObject> Data;

	UPROPERTY()
	FLinearColor Tint = FLinearColor::White;

	// 높은 우선순위가 이긴다. 같으면 등록 순서.
	UPROPERTY()
	int32 Priority = 0;

	UPROPERTY()
	EVBIndicatorViewerScope ViewerScope = EVBIndicatorViewerScope::AllLocalViewers;

	UPROPERTY()
	TWeakObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	bool bSuppressed = false;
};

// 복사한 손잡이는 별칭일 뿐 두 번째 소유가 아니다. 번호는 이 목록(월드) 안에서만 뜻이 있다.
struct VB_API FVBIndicatorHandle
{
	bool IsValid() const;
	uint64 GetId() const { return Id; }

	bool operator==(const FVBIndicatorHandle& Other) const
	{
		// 보통의 약참조 비교는 서로 다른 만료 객체를 같게 본다 - GC 뒤에도 식별, 해시를 지킨다.
		return Registry.HasSameIndexAndSerialNumber(Other.Registry) && Id == Other.Id;
	}

	bool operator!=(const FVBIndicatorHandle& Other) const { return !(*this == Other); }
	friend uint32 GetTypeHash(const FVBIndicatorHandle& Handle)
	{
		return HashCombineFast(GetTypeHash(Handle.Registry), GetTypeHash(Handle.Id));
	}

private:
	friend class UVBIndicatorRegistrySubsystem;
	friend struct FVBIndicatorRegistration;
	TWeakObjectPtr<UVBIndicatorRegistrySubsystem> Registry;
	uint64 Id = 0;
};

// 주된 길은 EndPlay, 빙의 해제의 명시적 Reset 이고, 소멸자는 두 번 불려도 무해한 뒷받침이다.
struct VB_API FVBIndicatorRegistration
{
	FVBIndicatorRegistration() = default;
	FVBIndicatorRegistration(UVBIndicatorRegistrySubsystem& Registry, const FVBIndicatorRequest& Request);
	~FVBIndicatorRegistration();
	FVBIndicatorRegistration(const FVBIndicatorRegistration&) = delete;
	FVBIndicatorRegistration& operator=(const FVBIndicatorRegistration&) = delete;
	FVBIndicatorRegistration(FVBIndicatorRegistration&& Other) noexcept;
	FVBIndicatorRegistration& operator=(FVBIndicatorRegistration&& Other) noexcept;

	void Reset();
	bool Update(const FVBIndicatorRequest& Request);
	bool IsValid() const { return Handle.IsValid(); }
	const FVBIndicatorHandle& GetHandle() const { return Handle; }

private:
	FVBIndicatorHandle Handle;
};
