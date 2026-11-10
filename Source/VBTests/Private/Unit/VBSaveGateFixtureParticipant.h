#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBSaveGateFixtureParticipant.generated.h"

UCLASS()
class UVBSaveGateFixtureParticipant : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()
public:
	TFunction<void(UVBSaveGame&, const FVBSaveContext&)> OnWrite;
	TFunction<void(const UVBSaveGame&, const FVBSaveContext&)> OnRead;
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override
	{
		if (OnWrite)
		{
			OnWrite(Save, Context);
		}
	}

	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override
	{
		if (OnRead)
		{
			OnRead(Save, Context);
		}
	}
};
