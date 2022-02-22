#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace sfh
{
	struct ConfigFile
	{
		struct IndexFileElement
		{
			// content
			std::wstring m_path;
		};

		struct MonitorProcessElement
		{
			// content
			std::wstring m_name;
		};

		uint32_t wmiPollInterval = 500;

		// content
		std::vector<IndexFileElement> m_indexFile;
		std::vector<MonitorProcessElement> m_monitorProcess;

		static std::unique_ptr<ConfigFile> ReadFromFile(const std::wstring& path);
		static void WriteToFile(const std::wstring& path, const ConfigFile& config);
	};


	struct FontDatabase
	{
		struct FontFaceElement
		{
			struct NameElement
			{
				// name
				enum NameType : size_t
				{
					Win32FamilyName = 0,
					FullName,
					PostScriptName
				} m_type;

				static constexpr const wchar_t* TYPEMAP[] = {
					L"Win32FamilyName",
					L"FullName",
					L"PostScriptName"
				};

				// content
				std::wstring m_name;

				// help functions
				NameElement() = default;

				NameElement(NameType nameType, std::wstring&& name)
					: m_type(nameType), m_name(name)
				{
				}

				bool operator<(const NameElement& rhs) const
				{
					if (this->m_type == rhs.m_type)
						return this->m_name < rhs.m_name;
					return this->m_type < rhs.m_type;
				}

				bool operator==(const NameElement& rhs) const
				{
					return this->m_type == rhs.m_type && this->m_name == rhs.m_name;
				}
			};

			// attribute
			std::wstring m_path;
			uint32_t m_index = std::numeric_limits<uint32_t>::max();
			// content
			std::vector<NameElement> m_names;
		};

		std::vector<FontFaceElement> m_fonts;

		static std::unique_ptr<FontDatabase> ReadFromFile(const std::wstring& path);
		static void WriteToFile(const std::wstring& path, const FontDatabase& db);
	};
}
