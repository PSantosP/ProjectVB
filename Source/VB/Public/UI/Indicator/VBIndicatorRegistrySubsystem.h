// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UI/Indicator/VBIndicatorTypes.h"
#include "VBIndicatorRegistrySubsystem.generated.h"

// 알림, 위젯 소유, 고르기, 투영, GAS 조회, 비동기 읽기는 하지 않는다 - 요청을 걸고 고치고 떼기만.
UCLASS()
class VB_API UVBIndicatorRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	FVBIndicatorHandle Add(const FVBIndicatorRequest& Request);
	// 실패한 고치기는 살아 있는 옛 등록을 그대로 둔다. 성공해도 번호, 순서는 안 바뀐다.
	bool Update(const FVBIndicatorHandle& Handle, const FVBIndicatorRequest& Request);
	bool Remove(const FVBIndicatorHandle& Handle);
	bool IsRegistered(const FVBIndicatorHandle& Handle) const;
	bool TryGet(const FVBIndicatorHandle& Handle, FVBIndicatorRequest& OutRequest) const;
	bool TryGetForViewer(const FVBIndicatorHandle& Handle, const APlayerController* Viewer,
		FVBIndicatorRequest& OutRequest) const;

	// 번호의 복사본, 오래된 것부터. 쓰기 전에 다시 찾는다 - 배열 참조를 밖으로 내주지 않는다.
	TArray<FVBIndicatorHandle> SnapshotForViewer(const APlayerController* Viewer) const;

private:
	bool IsLive(const FVBIndicatorRequest& Request) const;
	bool Owns(const FVBIndicatorHandle& Handle) const;
	void PruneExpired() const;
	const FVBIndicatorRequest* FindLive(const FVBIndicatorHandle& Handle) const;
	FVBIndicatorHandle MakeHandle(uint64 Id) const;

	// 요청의 참조는 모두 약참조다. const 읽기도 만료 항목을 걷어낼 수 있다. 콜백은 없다.
	mutable TMap<uint64, FVBIndicatorRequest> Entries;
	mutable TArray<uint64> RegistrationOrder;
	uint64 NextId = 1;
	bool bAcceptingRequests = false;
};
