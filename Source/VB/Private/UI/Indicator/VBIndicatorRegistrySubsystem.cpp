// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "UI/Indicator/VBIndicatorRegistrySubsystem.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/Indicator/VBIndicatorSelection.h"

bool FVBIndicatorHandle::IsValid() const
{
	const UVBIndicatorRegistrySubsystem* Owner = Registry.Get();
	return Owner && Owner->IsRegistered(*this);
}

FVBIndicatorRegistration::FVBIndicatorRegistration(UVBIndicatorRegistrySubsystem& Registry,
	const FVBIndicatorRequest& Request) : Handle(Registry.Add(Request))
{
}

FVBIndicatorRegistration::~FVBIndicatorRegistration()
{
	Reset();
}

FVBIndicatorRegistration::FVBIndicatorRegistration(FVBIndicatorRegistration&& Other) noexcept
	: Handle(Other.Handle)
{
	Other.Handle = {};
}

FVBIndicatorRegistration& FVBIndicatorRegistration::operator=(FVBIndicatorRegistration&& Other) noexcept
{
	if (this != &Other)
	{
		Reset();
		Handle = Other.Handle;
		Other.Handle = {};
	}

	return *this;
}

void FVBIndicatorRegistration::Reset()
{
	// 소유부터 지운다. 죽은 목록이 다른 월드의 목록이 되는 일은 없다.
	const FVBIndicatorHandle Old = Handle;
	Handle = {};
	if (UVBIndicatorRegistrySubsystem* Registry = Old.Registry.Get())
	{
		Registry->Remove(Old);
	}
}

bool FVBIndicatorRegistration::Update(const FVBIndicatorRequest& Request)
{
	UVBIndicatorRegistrySubsystem* Registry = Handle.Registry.Get();
	return Registry && Registry->Update(Handle, Request);
}

void UVBIndicatorRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bAcceptingRequests = true;
	// NextId 는 되돌리지 않는다 - 같은 하위 시스템 객체가 다시 초기화돼도.
}

void UVBIndicatorRegistrySubsystem::Deinitialize()
{
	check(IsInGameThread());
	bAcceptingRequests = false;
	Entries.Reset();
	RegistrationOrder.Reset();
	Super::Deinitialize();
}

bool UVBIndicatorRegistrySubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UVBIndicatorRegistrySubsystem::IsLive(const FVBIndicatorRequest& Request) const
{
	const UWorld* World = GetWorld();
	const USceneComponent* Anchor = Request.AnchorComponent.Get();
	if (!bAcceptingRequests || !World || World->bIsTearingDown || !Request.Group.IsValid()
		|| !Anchor || Anchor->IsBeingDestroyed() || Anchor->GetWorld() != World)
	{
		return false;
	}

	const AActor* AnchorOwner = Anchor->GetOwner();
	if (AnchorOwner && AnchorOwner->IsActorBeingDestroyed())
	{
		return false;
	}

	if (Request.ViewerScope == EVBIndicatorViewerScope::AllLocalViewers)
	{
		return true;
	}

	const APlayerController* Viewer = Request.Viewer.Get();
	return Request.ViewerScope == EVBIndicatorViewerScope::OneViewer
		&& Viewer && !Viewer->IsActorBeingDestroyed() && Viewer->GetWorld() == World;
}

bool UVBIndicatorRegistrySubsystem::Owns(const FVBIndicatorHandle& Handle) const
{
	return bAcceptingRequests && Handle.Id != 0 && Handle.Registry.Get() == this;
}

void UVBIndicatorRegistrySubsystem::PruneExpired() const
{
	RegistrationOrder.RemoveAll([this](uint64 Id)
	{
		const FVBIndicatorRequest* Request = Entries.Find(Id);
		if (Request && IsLive(*Request))
		{
			return false;
		}

		Entries.Remove(Id);
		return true;
	});
}

const FVBIndicatorRequest* UVBIndicatorRegistrySubsystem::FindLive(const FVBIndicatorHandle& Handle) const
{
	if (!Owns(Handle))
	{
		return nullptr;
	}

	const FVBIndicatorRequest* Request = Entries.Find(Handle.Id);
	if (Request && !IsLive(*Request))
	{
		Entries.Remove(Handle.Id);
		RegistrationOrder.Remove(Handle.Id);
		return nullptr;
	}

	return Request;
}

FVBIndicatorHandle UVBIndicatorRegistrySubsystem::MakeHandle(uint64 Id) const
{
	FVBIndicatorHandle Handle;
	Handle.Registry = const_cast<UVBIndicatorRegistrySubsystem*>(this);
	Handle.Id = Id;
	return Handle;
}

FVBIndicatorHandle UVBIndicatorRegistrySubsystem::Add(const FVBIndicatorRequest& Request)
{
	check(IsInGameThread());
	// 여기서 전체를 훑지 않는다(한꺼번에 생기면 N^2) - 만료 항목은 약참조뿐이라 다음 스냅숏이 걷어낸다.
	if (!IsLive(Request) || NextId == 0)
	{
		return {};
	}

	const uint64 Id = NextId++;
	Entries.Add(Id, Request);
	RegistrationOrder.Add(Id);
	return MakeHandle(Id);
}

bool UVBIndicatorRegistrySubsystem::Update(const FVBIndicatorHandle& Handle, const FVBIndicatorRequest& Request)
{
	check(IsInGameThread());
	if (!FindLive(Handle) || !IsLive(Request))
	{
		return false;
	}

	Entries.FindChecked(Handle.Id) = Request;
	return true;
}

bool UVBIndicatorRegistrySubsystem::Remove(const FVBIndicatorHandle& Handle)
{
	check(IsInGameThread());
	if (!Owns(Handle) || Entries.Remove(Handle.Id) == 0)
	{
		return false;
	}

	RegistrationOrder.Remove(Handle.Id);
	return true;
}

bool UVBIndicatorRegistrySubsystem::IsRegistered(const FVBIndicatorHandle& Handle) const
{
	check(IsInGameThread());
	return FindLive(Handle) != nullptr;
}

bool UVBIndicatorRegistrySubsystem::TryGet(const FVBIndicatorHandle& Handle, FVBIndicatorRequest& OutRequest) const
{
	check(IsInGameThread());
	OutRequest = {};
	const FVBIndicatorRequest* Request = FindLive(Handle);
	if (!Request)
	{
		return false;
	}

	OutRequest = *Request;
	return true;
}

bool UVBIndicatorRegistrySubsystem::TryGetForViewer(const FVBIndicatorHandle& Handle,
	const APlayerController* Viewer, FVBIndicatorRequest& OutRequest) const
{
	if (TryGet(Handle, OutRequest) && IsValid(Viewer) && Viewer->GetWorld() == GetWorld()
		&& VBIndicatorSelection::MatchesViewer(OutRequest, Viewer))
	{
		return true;
	}

	OutRequest = {};
	return false;
}

TArray<FVBIndicatorHandle> UVBIndicatorRegistrySubsystem::SnapshotForViewer(const APlayerController* Viewer) const
{
	check(IsInGameThread());
	PruneExpired();
	TArray<FVBIndicatorHandle> Result;
	if (!IsValid(Viewer) || Viewer->GetWorld() != GetWorld())
	{
		return Result;
	}

	Result.Reserve(Entries.Num());
	for (uint64 Id : RegistrationOrder)
	{
		if (VBIndicatorSelection::MatchesViewer(Entries.FindChecked(Id), Viewer))
		{
			Result.Add(MakeHandle(Id));
		}
	}

	return Result;
}
