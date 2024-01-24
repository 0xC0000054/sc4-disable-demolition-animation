////////////////////////////////////////////////////////////////////////////
//
// This file is part of sc4-disable-demolition-animation, a DLL Plugin for
// SimCity 4 that disables the demolition animation.
//
// Copyright (c) 2024 Nicholas Hayes
//
// This file is licensed under terms of the MIT License.
// See LICENSE.txt for more information.
//
////////////////////////////////////////////////////////////////////////////

#include "version.h"
#include "Logger.h"
#include "SC4VersionDetection.h"
#include "cIGZCOM.h"
#include "cIGZFrameWork.h"
#include "cISC4Occupant.h"
#include "cISCProperty.h"
#include "cISCPropertyHolder.h"
#include "cIGZVariant.h"
#include "cRZCOMDllDirector.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <Windows.h>
#include "wil/resource.h"
#include "wil/win32_helpers.h"

#include "EASTLConfigSC4.h"
#include "EASTL\vector.h"

static constexpr uint32_t kDisableDemolitionAnimationDirectorID = 0xD9A81BA1;

static constexpr std::string_view PluginLogFileName = "SC4DisableDemolitionAnimation.log";

namespace
{
	std::string GetOccupantExemplarName(cISC4Occupant* pOccupant)
	{
		std::string name;

		cISCPropertyHolder* propertyHolder = pOccupant->AsPropertyHolder();

		constexpr uint32_t kExemplarName = 0x00000020;

		cISCProperty* exemplarName = propertyHolder->GetProperty(kExemplarName);

		if (exemplarName)
		{
			const cIGZVariant* propertyValue = exemplarName->GetPropertyValue();
			const uint16_t type = propertyValue->GetType();

			if (type == cIGZVariant::Type::RZCharArray)
			{
				const char* szName = propertyValue->RefRZChar();

				name.assign(szName);
			}
		}

		return name;
	}

	bool __cdecl IsOccupantTooSmallForDemolitionAnimation(cISC4Occupant* pOccupant, float* unknown)
	{
#ifdef _DEBUG
		std::string name = GetOccupantExemplarName(pOccupant);

		if (!name.empty())
		{
			Logger& logger = Logger::GetInstance();

			logger.WriteLineFormatted(
				LogLevel::Debug,
				"Demolished occupant '%s'.",
				name.c_str());
		}
#endif // _DEBUG

		return true;
	}

	void InstallCallHook(uintptr_t targetAddress, void* pfnFunc)
	{
		// Allow the executable memory to be written to.
		DWORD oldProtect = 0;
		THROW_IF_WIN32_BOOL_FALSE(VirtualProtect(
			reinterpret_cast<LPVOID>(targetAddress),
			5,
			PAGE_EXECUTE_READWRITE,
			&oldProtect));

		// Patch the memory at the specified address.
		*((uint8_t*)targetAddress) = 0xE8;
		*((uintptr_t*)(targetAddress + 1)) = ((uintptr_t)pfnFunc) - targetAddress - 5;
	}

	void InstallDemolitionAnimationHook()
	{
		Logger& logger = Logger::GetInstance();

		const uint16_t gameVersion = SC4VersionDetection::GetInstance().GetGameVersion();

		// Before SC4 picks the demolition animation to use for an occupant, it calls a function
		// to determine if the occupant is too small for a demolition animation.
		//
		// We redirect that function call to our own version of the function, which always reports
		// that the occupant is too small for a demolition animation.
		// This has the effect of disabling the demolition animation for all occupants.

		uintptr_t hookTargetAddress = 0;

		switch (gameVersion)
		{
		case 641:
			hookTargetAddress = 0x4673bf;
			break;
		}

		if (hookTargetAddress != 0)
		{
			try
			{
				InstallCallHook(hookTargetAddress, &IsOccupantTooSmallForDemolitionAnimation);

				logger.WriteLine(LogLevel::Info, "Disabled the occupant demolition animations.");
			}
			catch (const wil::ResultException& e)
			{
				logger.WriteLineFormatted(
					LogLevel::Error,
					"Failed to install the demolition animations patch.\n%s",
					e.what());
			}
		}
		else
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"Unsupported game version: %d",
				gameVersion);
		}
	}
}

class DisableDemolitionAnimationDllDirector : public cRZCOMDllDirector
{
public:

	DisableDemolitionAnimationDllDirector()
	{
		std::filesystem::path dllFolderPath = GetDllFolderPath();

		std::filesystem::path logFilePath = dllFolderPath;
		logFilePath /= PluginLogFileName;

		Logger& logger = Logger::GetInstance();

#ifdef _DEBUG
		logger.Init(logFilePath, LogLevel::Debug);
#else
		logger.Init(logFilePath, LogLevel::Error);
#endif // _DEBUG

		logger.WriteLogFileHeader("SC4DisableDemolitionAnimation v" PLUGIN_VERSION_STR);
	}

	uint32_t GetDirectorID() const
	{
		return kDisableDemolitionAnimationDirectorID;
	}

	bool OnStart(cIGZCOM * pCOM)
	{
		InstallDemolitionAnimationHook();

		return true;
	}

private:

	std::filesystem::path GetDllFolderPath()
	{
		wil::unique_cotaskmem_string modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());

		std::filesystem::path temp(modulePath.get());

		return temp.parent_path();
	}
};

cRZCOMDllDirector* RZGetCOMDllDirector() {
	static DisableDemolitionAnimationDllDirector sDirector;
	return &sDirector;
}