// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBSaveParticipantSubsystem.h"

#include "Engine/World.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "SaveSystem/VBSaveGame.h"
#include "UObject/StrongObjectPtr.h"

void UVBSaveParticipantSubsystem::Register(IVBSaveParticipant* Participant)
{
	if (UVBSaveParticipantSubsystem* Registry = FindFor(Participant))
	{
		Registry->Participants.AddUnique(TWeakInterfacePtr<IVBSaveParticipant>(Participant));
	}
}

void UVBSaveParticipantSubsystem::Unregister(IVBSaveParticipant* Participant)
{
	if (UVBSaveParticipantSubsystem* Registry = FindFor(Participant))
	{
		const UObject* Object = Cast<UObject>(Participant);
		Registry->Participants.RemoveAll([Object](const TWeakInterfacePtr<IVBSaveParticipant>& Entry)
		{
			return !Entry.IsValid() || Entry.GetObject() == Object;
		});
	}
}

TArray<IVBSaveParticipant*> UVBSaveParticipantSubsystem::GetParticipants()
{
	Participants.RemoveAll([](const TWeakInterfacePtr<IVBSaveParticipant>& Entry) { return !Entry.IsValid(); });
	TArray<IVBSaveParticipant*> Result;
	Result.Reserve(Participants.Num());
	for (const TWeakInterfacePtr<IVBSaveParticipant>& Entry : Participants)
	{
		Result.Add(Entry.Get());
	}

	return Result;
}

int32 UVBSaveParticipantSubsystem::ReadAll(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	return ReadAll(Save, Context, [] { return true; });
}

int32 UVBSaveParticipantSubsystem::ReadAll(const UVBSaveGame& Save, const FVBSaveContext& Context,
	TFunctionRef<bool()> IsCurrent)
{
	if (bReadCycleActive || !IsValid(&Save) || !IsValid(Context.World) || Context.World != GetWorld())
	{
		return 0;
	}

	const TStrongObjectPtr<UVBSaveParticipantSubsystem> RegistryLifetime(this);
	const TStrongObjectPtr<const UVBSaveGame> SaveLifetime(&Save);
	const TStrongObjectPtr<UWorld> WorldLifetime(Context.World);
	const FVBSaveContext ReadContext = Context;
	const auto CanContinue = [&]
	{
		return IsValid(this) && IsValid(WorldLifetime.Get()) && !WorldLifetime->bIsTearingDown
			&& GetWorld() == WorldLifetime.Get() && IsCurrent();
	};
	if (!CanContinue())
	{
		return 0;
	}

	TGuardValue<bool> ReadCycle(bReadCycleActive, true);
	const TArray<TWeakInterfacePtr<IVBSaveParticipant>> Readers = Participants;
	int32 Delivered = 0;
	{
		TGuardValue<bool> Applying(bApplyingSave, true);
		for (const TWeakInterfacePtr<IVBSaveParticipant>& Reader : Readers)
		{
			if (!CanContinue())
			{
				return 0;
			}

			if (!Reader.IsValid() || !Participants.Contains(Reader))
			{
				continue;
			}

			UObject* Object = Reader.GetObject();
			const UActorComponent* Component = Cast<UActorComponent>(Object);
			const AActor* Actor = Component ? Component->GetOwner() : Cast<AActor>(Object);
			if (!IsValid(Object) || Object->GetWorld() != WorldLifetime.Get()
				|| (Component && !Component->IsRegistered()) || (Actor && Actor->IsActorBeingDestroyed()))
			{
				continue;
			}

			const TStrongObjectPtr<UObject> ReaderLifetime(Object);
			Reader->ReadSave(*SaveLifetime.Get(), ReadContext);
			++Delivered;
			if (!CanContinue())
			{
				return 0;
			}
		}
	}

	if (Delivered == 0 || !CanContinue())
	{
		return 0;
	}

	OnSaveApplied.Broadcast();
	if (!CanContinue())
	{
		return 0;
	}

	OnSaveFinalized.Broadcast();
	return CanContinue() ? Delivered : 0;
}

UVBSaveParticipantSubsystem* UVBSaveParticipantSubsystem::FindFor(IVBSaveParticipant* Participant)
{
	const UObject* Object = Cast<UObject>(Participant);
	const UWorld* World = Object ? Object->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr;
}
