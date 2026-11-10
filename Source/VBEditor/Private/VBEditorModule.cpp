// VB: approved general editor-module home; M01b node implementation follows separately.
#include "Modules/ModuleManager.h"
#include "VBRigVMCookFilter.h"

class FVBEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// 쿡 커맨드렛에서만 켠다 - 그 프로세스는 원본 패키지를 저장하지 않으니 남은 RF_Transient 가 원본에 닿을 길이 없다.
		RigVMCookFilter = MakeUnique<FVBRigVMCookFilter>([] { return IsRunningCookCommandlet(); });
		RigVMCookFilter->Register();
	}

	virtual void ShutdownModule() override
	{
		if (RigVMCookFilter)
		{
			RigVMCookFilter->Unregister();
			RigVMCookFilter->RestoreAll();
			RigVMCookFilter.Reset();
		}
	}

private:
	TUniquePtr<FVBRigVMCookFilter> RigVMCookFilter;
};

IMPLEMENT_MODULE(FVBEditorModule, VBEditor)
