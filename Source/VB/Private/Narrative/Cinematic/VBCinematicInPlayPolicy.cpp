#include "Narrative/Cinematic/VBCinematicInPlayPolicy.h"
#include "Narrative/Cinematic/VBCinematicInPlayLease.h"

#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "VB/VB.h"

bool UVBCinematicInPlayPolicy::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UVBCinematicInPlayPolicy::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bStopping = false;
	Director = Collection.InitializeDependency<UVBCinematicDirectorSubsystem>();
	// 걸지 못하면 진행 중 장면이 모두 거절된다 - 경고 없이 무시하지 않는다.
	if (!Director.IsValid()
		|| !Director->InstallInPlaySafetyProvider(this, FVBCinematicAcquireSafety::CreateUObject(this, &ThisClass::AcquireForController)))
	{
		VB_LOG(Error, "진행 중 시네마틱 정책을 감독에 걸지 못했다 - 레벨 안 장면이 모두 거절된다");
	}
}

TSharedPtr<FVBCinematicGameLease> UVBCinematicInPlayPolicy::AcquireForController(APlayerController* Controller, FGuid AttemptId)
{
	if (bStopping || (ActiveLease && !ActiveLease->IsFinished()))
	{
		return nullptr;
	}

	TSharedPtr<FVBInPlayCinematicLease> Lease = MakeShared<FVBInPlayCinematicLease>(this, Controller, UVBSaveGameSubsystem::Get(this));
	if (!Lease->Acquire())
	{
		VB_LOG(Log, "진행 중 시네마틱 임대 거절: 플레이어가 조종 중이 아니거나 저장을 기록하는 중");
		return nullptr;
	}

	ActiveLease = Lease;
	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickRestore));
	}

	return Lease;
}

bool UVBCinematicInPlayPolicy::TickRestore(float DeltaTime)
{
	const TSharedPtr<FVBInPlayCinematicLease> Guard = ActiveLease;
	if (bStopping || !Guard || Guard->IsFinished())
	{
		ActiveLease.Reset();
		TickerHandle.Reset();
		return false;
	}

	Guard->TickRestore();
	return true;
}

void UVBCinematicInPlayPolicy::Stop()
{
	if (bStopping)
	{
		return;
	}

	bStopping = true;
	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	if (Director.IsValid())
	{
		Director->RemoveInPlaySafetyProvider(this);
	}

	if (ActiveLease)
	{
		ActiveLease->Finish(false);
	}

	ActiveLease.Reset();
}

void UVBCinematicInPlayPolicy::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

void UVBCinematicInPlayPolicy::OnWorldEndPlay(UWorld& World)
{
	Stop();
	Super::OnWorldEndPlay(World);
}
