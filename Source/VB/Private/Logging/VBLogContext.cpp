#include "Logging/VBLogContext.h"

#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "UObject/Package.h"

FVBLogContext VBResolveLogContext(const UObject* Context)
{
	FVBLogContext Result;
	if (!IsValid(Context))
	{
		return Result;
	}

	const AActor* Actor = Cast<AActor>(Context);
	if (const UActorComponent* Component = Cast<UActorComponent>(Context))
	{
		Actor = Component->GetOwner();
		if (!IsValid(Actor))
		{
			return Result;
		}
	}

	if (Actor && !IsValid(Actor))
	{
		return Result;
	}

	const UWorld* World = Actor ? Actor->GetWorld() : Context->GetWorld();
	if (!IsValid(World))
	{
		return Result;
	}

	Result.NetMode = World->GetNetMode();
	if (Actor)
	{
		Result.Role = Actor->GetLocalRole();
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			if (IsValid(Pawn->GetController()))
			{
				Result.Locality = Pawn->IsLocallyControlled() ? EVBLogLocality::Local : EVBLogLocality::Remote;
			}
		}
		else if (const AController* Controller = Cast<AController>(Actor))
		{
			Result.Locality = Controller->IsLocalController() ? EVBLogLocality::Local : EVBLogLocality::Remote;
		}
		else
		{
			Result.Locality = EVBLogLocality::NotApplicable;
		}
	}
#if WITH_EDITORONLY_DATA
	if (World->WorldType != EWorldType::PIE)
	{
		Result.PIE = EVBLogPIE::None;
	}
	else if (const UPackage* Package = World->GetPackage(); IsValid(Package) && Package->GetPIEInstanceID() != INDEX_NONE)
	{
		Result.PIE = EVBLogPIE::Instance;
		Result.PIEInstance = Package->GetPIEInstanceID();
	}
#endif
	return Result;
}

FString VBFormatLogContext(const FVBLogContext& Context)
{
	const TCHAR* Net = TEXT("Unknown");
	if (Context.NetMode.IsSet())
	{
		switch (Context.NetMode.GetValue())
		{
		case NM_Standalone: Net = TEXT("Standalone"); break;
		case NM_ListenServer: Net = TEXT("ListenServer"); break;
		case NM_DedicatedServer: Net = TEXT("DedicatedServer"); break;
		case NM_Client: Net = TEXT("Client"); break;
		default: break;
		}
	}

	const TCHAR* Role = TEXT("Unknown");
	if (Context.Role.IsSet())
	{
		switch (Context.Role.GetValue())
		{
		case ROLE_Authority: Role = TEXT("Authority"); break;
		case ROLE_AutonomousProxy: Role = TEXT("AutonomousProxy"); break;
		case ROLE_SimulatedProxy: Role = TEXT("SimulatedProxy"); break;
		case ROLE_None: Role = TEXT("None"); break;
		default: break;
		}
	}

	const TCHAR* Local = TEXT("Unknown");
	switch (Context.Locality)
	{
	case EVBLogLocality::Local: Local = TEXT("Local"); break;
	case EVBLogLocality::Remote: Local = TEXT("Remote"); break;
	case EVBLogLocality::NotApplicable: Local = TEXT("NotApplicable"); break;
	default: break;
	}

	FString PIE = TEXT("Unknown");
	if (Context.PIE == EVBLogPIE::None)
	{
		PIE = TEXT("None");
	}
	else if (Context.PIE == EVBLogPIE::Instance && Context.PIEInstance >= 0)
	{
		PIE = FString::FromInt(Context.PIEInstance);
	}

	return FString::Printf(TEXT("Net=%s Role=%s Local=%s PIE=%s"), Net, Role, Local, *PIE);
}

FString VBGetLogContext(const UObject* Context) { return VBFormatLogContext(VBResolveLogContext(Context)); }
