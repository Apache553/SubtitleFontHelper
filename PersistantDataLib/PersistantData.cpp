#include "PersistantData.h"

#include <vector>
#include <cassert>
#include <stdexcept>
#include <atomic>
#include <cwctype>
#include <memory>
#include <variant>

#include <Windows.h>
#undef max
#include <combaseapi.h>
#include <propvarutil.h>
#include <shellapi.h>
#pragma comment(lib,"Shlwapi.lib")
#include <MsXml2.h>
#pragma comment(lib,"msxml2.lib")
#include <wil/resource.h>
#include <wil/com.h>


namespace
{
	uint32_t wcstou32(const wchar_t* str, int length)
	{
		uint64_t ret = 0;
		for (int i = 0; i < length; ++i)
		{
			if (str[i] > L'9' || str[i] < L'0')
				throw std::out_of_range("unexpected character in numeric string");
			ret *= 10;
			ret += str[i] - L'0';
			if (ret > std::numeric_limits<uint32_t>::max())
				throw std::out_of_range("number too large");
		}
		return static_cast<uint32_t>(ret);
	}

	class SimpleSAXContentHandler : public ISAXContentHandler
	{
	private:
		ULONG m_refCount = 1;
	public:
		virtual ~SimpleSAXContentHandler() = default;

		HRESULT STDMETHODCALLTYPE QueryInterface(const IID& riid, void** ppvObject) override
		{
			if (ppvObject == nullptr)
				return E_INVALIDARG;
			if (riid == IID_IUnknown)
			{
				*ppvObject = static_cast<IUnknown*>(this);
			}
			else if (riid == IID_ISAXContentHandler)
			{
				*ppvObject = static_cast<ISAXContentHandler*>(this);
			}
			else
			{
				return E_NOINTERFACE;
			}
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++m_refCount;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			ULONG refCount = --m_refCount;
			if (refCount == 0)
				delete this;
			return refCount;
		}

		HRESULT STDMETHODCALLTYPE putDocumentLocator(ISAXLocator* pLocator) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE startDocument() override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE endDocument() override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE startPrefixMapping(const wchar_t* pwchPrefix, int cchPrefix, const wchar_t* pwchUri,
		                                             int cchUri) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE endPrefixMapping(const wchar_t* pwchPrefix, int cchPrefix) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE startElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
		                                       const wchar_t* pwchLocalName,
		                                       int cchLocalName, const wchar_t* pwchQName, int cchQName,
		                                       ISAXAttributes* pAttributes) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE endElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
		                                     const wchar_t* pwchLocalName,
		                                     int cchLocalName, const wchar_t* pwchQName, int cchQName) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE characters(const wchar_t* pwchChars, int cchChars) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE ignorableWhitespace(const wchar_t* pwchChars, int cchChars) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE processingInstruction(const wchar_t* pwchTarget, int cchTarget,
		                                                const wchar_t* pwchData,
		                                                int cchData) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE skippedEntity(const wchar_t* pwchName, int cchName) override
		{
			return S_OK;
		}
	};

	class ConfigSAXContentHandler : public SimpleSAXContentHandler
	{
	private:
		enum class ElementType:size_t
		{
			Document = 0,
			RootElement,
			IndexFileElement,
			MonitorElement
		};

		std::unique_ptr<sfh::ConfigFile> m_config;
		std::vector<ElementType> m_status;

	public:
		HRESULT STDMETHODCALLTYPE startDocument() override
		{
			m_config = std::make_unique<sfh::ConfigFile>();
			m_status.clear();
			m_status.emplace_back(ElementType::Document);
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE endDocument() override
		{
			if (!m_status.empty() && m_status.back() == ElementType::Document)
				m_status.pop_back();
			if (m_status.empty())
				return S_OK;
			return E_FAIL;
		}

		HRESULT STDMETHODCALLTYPE startElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
		                                       const wchar_t* pwchLocalName,
		                                       int cchLocalName, const wchar_t* pwchQName, int cchQName,
		                                       ISAXAttributes* pAttributes) override
		{
			if (m_status.empty())
				return E_FAIL;
			switch (m_status.back())
			{
			case ElementType::Document:
				if (wcsncmp(pwchLocalName, L"ConfigFile", cchLocalName) == 0)
				{
					m_status.emplace_back(ElementType::RootElement);
					const wchar_t* attrValue;
					int attrLength;
					if (SUCCEEDED(
						pAttributes->getValueFromName(L"", 0, L"wmiPollInterval", 15, &attrValue, &attrLength)))
					{
						try
						{
							m_config->wmiPollInterval = wcstou32(attrValue, attrLength);
						}
						catch (...)
						{
							// don't let exceptions travel across dll
							return E_FAIL;
						}
					}
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::RootElement:
				if (wcsncmp(pwchLocalName, L"IndexFile", cchLocalName) == 0)
				{
					m_config->m_indexFile.emplace_back();
					m_status.emplace_back(ElementType::IndexFileElement);
				}
				else if (wcsncmp(pwchLocalName, L"MonitorProcess", cchLocalName) == 0)
				{
					m_config->m_monitorProcess.emplace_back();
					m_status.emplace_back(ElementType::MonitorElement);
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::IndexFileElement:
				return E_FAIL;
				break;
			case ElementType::MonitorElement:
				return E_FAIL;
				break;
			default:
				return E_FAIL;
			}
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE endElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
		                                     const wchar_t* pwchLocalName,
		                                     int cchLocalName, const wchar_t* pwchQName, int cchQName) override
		{
			if (m_status.empty())
				return E_FAIL;
			switch (m_status.back())
			{
			case ElementType::RootElement:
				if (wcsncmp(pwchLocalName, L"ConfigFile", cchLocalName) == 0)
				{
					m_status.pop_back();
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::IndexFileElement:
				if (wcsncmp(pwchLocalName, L"IndexFile", cchLocalName) == 0)
				{
					m_status.pop_back();
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::MonitorElement:
				if (wcsncmp(pwchLocalName, L"MonitorProcess", cchLocalName) == 0)
				{
					m_status.pop_back();
				}
				else
				{
					return E_FAIL;
				}
				break;
			default:
				return E_FAIL;
			}
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE characters(const wchar_t* pwchChars, int cchChars) override
		{
			if (m_status.empty())
				return E_FAIL;
			switch (m_status.back())
			{
			case ElementType::IndexFileElement:
				m_config->m_indexFile.back().m_path.assign(pwchChars, cchChars);
				break;
			case ElementType::MonitorElement:
				m_config->m_monitorProcess.back().m_name.assign(pwchChars, cchChars);
				break;
			}
			// ignore unexpected characters
			return S_OK;
		}

		std::unique_ptr<sfh::ConfigFile> GetConfigFile()
		{
			return std::move(m_config);
		}
	};

	class FontDatabaseSAXContentHandler : public SimpleSAXContentHandler
	{
	private:
		enum class ElementType :size_t
		{
			Document = 0,
			RootElement,
			FontFaceElement,
			Win32FamilyNameElement,
			FullNameElement,
			PostScriptNameElement
		};

		std::unique_ptr<sfh::FontDatabase> m_db;
		std::vector<ElementType> m_status;

	public:
		HRESULT STDMETHODCALLTYPE startDocument() override
		{
			m_db = std::make_unique<sfh::FontDatabase>();
			m_status.clear();
			m_status.emplace_back(ElementType::Document);
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE endDocument() override
		{
			if (!m_status.empty() && m_status.back() == ElementType::Document)
				m_status.pop_back();
			if (m_status.empty())
				return S_OK;
			return E_FAIL;
		}

		HRESULT STDMETHODCALLTYPE startElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
		                                       const wchar_t* pwchLocalName,
		                                       int cchLocalName, const wchar_t* pwchQName, int cchQName,
		                                       ISAXAttributes* pAttributes) override
		{
			if (m_status.empty())
				return E_FAIL;
			switch (m_status.back())
			{
			case ElementType::Document:
				if (wcsncmp(pwchLocalName, L"FontDatabase", cchLocalName) == 0)
				{
					m_status.emplace_back(ElementType::RootElement);
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::RootElement:
				if (wcsncmp(pwchLocalName, L"FontFace", cchLocalName) == 0)
				{
					m_db->m_fonts.emplace_back();
					m_status.emplace_back(ElementType::FontFaceElement);
					const wchar_t* attrValue;
					int attrLength;
					if (FAILED(pAttributes->getValueFromName(L"", 0, L"path", 4, &attrValue, &attrLength)))
						return E_FAIL;
					m_db->m_fonts.back().m_path.assign(attrValue, attrLength);
					if (FAILED(pAttributes->getValueFromName(L"", 0, L"index", 5, &attrValue, &attrLength)))
						return E_FAIL;
					try
					{
						m_db->m_fonts.back().m_index = wcstou32(attrValue, attrLength);
					}
					catch (...)
					{
						// don't let exceptions travel across dll
						return E_FAIL;
					}
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::FontFaceElement:
				if (wcsncmp(pwchLocalName, L"Win32FamilyName", cchLocalName) == 0)
				{
					m_status.emplace_back(ElementType::Win32FamilyNameElement);
					m_db->m_fonts.back().m_names.emplace_back();
					m_db->m_fonts.back().m_names.back().m_type =
						sfh::FontDatabase::FontFaceElement::NameElement::Win32FamilyName;
				}
				else if (wcsncmp(pwchLocalName, L"FullName", cchLocalName) == 0)
				{
					m_status.emplace_back(ElementType::FullNameElement);
					m_db->m_fonts.back().m_names.emplace_back();
					m_db->m_fonts.back().m_names.back().m_type =
						sfh::FontDatabase::FontFaceElement::NameElement::FullName;
				}
				else if (wcsncmp(pwchLocalName, L"PostScriptName", cchLocalName) == 0)
				{
					m_status.emplace_back(ElementType::PostScriptNameElement);
					m_db->m_fonts.back().m_names.emplace_back();
					m_db->m_fonts.back().m_names.back().m_type =
						sfh::FontDatabase::FontFaceElement::NameElement::PostScriptName;
				}
				else
				{
					return E_FAIL;
				}
				break;
			default:
				return E_FAIL;
			}
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE endElement(const wchar_t* pwchNamespaceUri, int cchNamespaceUri,
		                                     const wchar_t* pwchLocalName,
		                                     int cchLocalName, const wchar_t* pwchQName, int cchQName) override
		{
			if (m_status.empty())
				return E_FAIL;
			switch (m_status.back())
			{
			case ElementType::RootElement:
				if (wcsncmp(pwchLocalName, L"FontDatabase", cchLocalName) == 0)
				{
					m_status.pop_back();
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::FontFaceElement:
				if (wcsncmp(pwchLocalName, L"FontFace", cchLocalName) == 0)
				{
					m_status.pop_back();
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::Win32FamilyNameElement:
				if (wcsncmp(pwchLocalName, L"Win32FamilyName", cchLocalName) == 0)
				{
					m_status.pop_back();
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::FullNameElement:
				if (wcsncmp(pwchLocalName, L"FullName", cchLocalName) == 0)
				{
					m_status.pop_back();
				}
				else
				{
					return E_FAIL;
				}
				break;
			case ElementType::PostScriptNameElement:
				if (wcsncmp(pwchLocalName, L"PostScriptName", cchLocalName) == 0)
				{
					m_status.pop_back();
				}
				else
				{
					return E_FAIL;
				}
				break;
			default:
				return E_FAIL;
			}
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE characters(const wchar_t* pwchChars, int cchChars) override
		{
			if (m_status.empty())
				return E_FAIL;
			switch (m_status.back())
			{
			case ElementType::Win32FamilyNameElement:
			case ElementType::FullNameElement:
			case ElementType::PostScriptNameElement:
				m_db->m_fonts.back().m_names.back().m_name.assign(pwchChars, cchChars);
			}
			// ignore unexpected characters
			return S_OK;
		}

		std::unique_ptr<sfh::FontDatabase> GetFontDatabase()
		{
			return std::move(m_db);
		}
	};
}

std::unique_ptr<sfh::ConfigFile> sfh::ConfigFile::ReadFromFile(const std::wstring& path)
{
	auto com = wil::CoInitializeEx();

	wil::unique_variant pathVariant;
	wil::com_ptr<IStream> stream;
	THROW_IF_FAILED(
		SHCreateStreamOnFileEx(
			path.c_str(),
			STGM_FAILIFTHERE | STGM_READ | STGM_SHARE_EXCLUSIVE,
			FILE_ATTRIBUTE_NORMAL,
			FALSE,
			nullptr,
			stream.put()));
	InitVariantFromUnknown(stream.query<IUnknown>().get(), pathVariant.addressof());

	auto saxReader = wil::CoCreateInstance<ISAXXMLReader>(CLSID_SAXXMLReader30);
	wil::com_ptr<ConfigSAXContentHandler> handler(new ConfigSAXContentHandler);
	THROW_IF_FAILED(saxReader->putContentHandler(handler.get()));
	THROW_IF_FAILED_MSG(saxReader->parse(pathVariant), "BAD CONFIG: %ws", path.c_str());

	return handler->GetConfigFile();
}

std::unique_ptr<sfh::FontDatabase> sfh::FontDatabase::ReadFromFile(const std::wstring& path)
{
	auto com = wil::CoInitializeEx();

	wil::unique_variant pathVariant;
	wil::com_ptr<IStream> stream;
	THROW_IF_FAILED(
		SHCreateStreamOnFileEx(
			path.c_str(),
			STGM_FAILIFTHERE | STGM_READ | STGM_SHARE_EXCLUSIVE,
			FILE_ATTRIBUTE_NORMAL,
			FALSE,
			nullptr,
			stream.put()));
	InitVariantFromUnknown(stream.query<IUnknown>().get(), pathVariant.addressof());

	auto saxReader = wil::CoCreateInstance<ISAXXMLReader>(CLSID_SAXXMLReader30);
	wil::com_ptr<FontDatabaseSAXContentHandler> handler(new FontDatabaseSAXContentHandler);
	THROW_IF_FAILED(saxReader->putContentHandler(handler.get()));
	THROW_IF_FAILED_MSG(saxReader->parse(pathVariant), "BAD FONTDATABASE: %ws", path.c_str());

	return handler->GetFontDatabase();
}

namespace sfh
{
	void WriteDocumentToFile(wil::com_ptr<IStream> stream, wil::com_ptr<IXMLDOMDocument> document)
	{
		auto mxWriter = wil::CoCreateInstance<IMXWriter>(CLSID_MXXMLWriter30);
		auto saxReader = wil::CoCreateInstance<ISAXXMLReader>(CLSID_SAXXMLReader30);

		wil::unique_variant output;
		wil::unique_variant unk;

		THROW_IF_FAILED(mxWriter->put_encoding(wil::make_bstr(L"UTF-8").get()));
		THROW_IF_FAILED(mxWriter->put_standalone(VARIANT_TRUE));
		THROW_IF_FAILED(mxWriter->put_indent(VARIANT_TRUE));
		InitVariantFromUnknown(stream.query<IUnknown>().get(), unk.reset_and_addressof());
		THROW_IF_FAILED(mxWriter->put_output(unk));

		THROW_IF_FAILED(saxReader->putContentHandler(mxWriter.query<ISAXContentHandler>().get()));
		InitVariantFromUnknown(mxWriter.query<IUnknown>().get(), unk.reset_and_addressof());
		THROW_IF_FAILED(saxReader->putProperty(L"http://xml.org/sax/properties/lexical-handler", unk));
		THROW_IF_FAILED(saxReader->putProperty(L"http://xml.org/sax/properties/declaration-handler", unk));
		InitVariantFromUnknown(document.query<IUnknown>().get(), unk.reset_and_addressof());
		THROW_IF_FAILED(saxReader->parse(unk));
	}

	wil::com_ptr<IXMLDOMDocument> ConfigFileToDocument(const ConfigFile& config)
	{
		auto document = wil::CoCreateInstance<IXMLDOMDocument>(CLSID_DOMDocument30);

		wil::com_ptr<IXMLDOMElement> rootElement;
		THROW_IF_FAILED(document->createElement(wil::make_bstr(L"ConfigFile").get(), rootElement.put()));
		wil::unique_variant value;
		InitVariantFromString(std::to_wstring(config.wmiPollInterval).c_str(), value.addressof());
		THROW_IF_FAILED(rootElement->setAttribute(wil::make_bstr(L"wmiPollInterval").get(), value));
		for (auto& indexFile : config.m_indexFile)
		{
			wil::com_ptr<IXMLDOMElement> indexFileElement;
			THROW_IF_FAILED(document->createElement(wil::make_bstr(L"IndexFile").get(), indexFileElement.put()));

			THROW_IF_FAILED(indexFileElement->put_text(wil::make_bstr(indexFile.m_path.c_str()).get()));
			THROW_IF_FAILED(rootElement->appendChild(indexFileElement.get(), nullptr));
		}
		for (auto& monitorProcess : config.m_monitorProcess)
		{
			wil::com_ptr<IXMLDOMElement> monitorProcessElement;
			THROW_IF_FAILED(
				document->createElement(wil::make_bstr(L"MonitorProcess").get(), monitorProcessElement.put()));

			THROW_IF_FAILED(monitorProcessElement->put_text(wil::make_bstr(monitorProcess.m_name.c_str()).get()));
			THROW_IF_FAILED(rootElement->appendChild(monitorProcessElement.get(), nullptr));
		}

		THROW_IF_FAILED(document->putref_documentElement(rootElement.get()));

		return document;
	}

	wil::com_ptr<IXMLDOMDocument> FontDatabaseToDocument(const FontDatabase& db)
	{
		auto document = wil::CoCreateInstance<IXMLDOMDocument>(CLSID_DOMDocument30);

		wil::com_ptr<IXMLDOMElement> rootElement;
		THROW_IF_FAILED(document->createElement(wil::make_bstr(L"FontDatabase").get(), rootElement.put()));

		for (auto& font : db.m_fonts)
		{
			wil::com_ptr<IXMLDOMElement> fontfaceElement;
			THROW_IF_FAILED(document->createElement(wil::make_bstr(L"FontFace").get(), fontfaceElement.put()));

			wil::unique_variant value;

			InitVariantFromString(font.m_path.c_str(), value.reset_and_addressof());
			THROW_IF_FAILED(fontfaceElement->setAttribute(wil::make_bstr(L"path").get(), value));
			InitVariantFromString(std::to_wstring(font.m_index).c_str(), value.reset_and_addressof());
			THROW_IF_FAILED(fontfaceElement->setAttribute(wil::make_bstr(L"index").get(), value));

			for (auto& name : font.m_names)
			{
				wil::com_ptr<IXMLDOMElement> nameElement;
				assert(name.m_type < std::extent_v<decltype(FontDatabase::FontFaceElement::NameElement::TYPEMAP)>);
				THROW_IF_FAILED(
					document->createElement(wil::make_bstr(FontDatabase::FontFaceElement::NameElement::TYPEMAP[name
						.m_type]).get(), nameElement.put()));
				THROW_IF_FAILED(nameElement->put_text(wil::make_bstr(name.m_name.c_str()).get()));
				THROW_IF_FAILED(fontfaceElement->appendChild(nameElement.get(), nullptr));
			}
			THROW_IF_FAILED(rootElement->appendChild(fontfaceElement.get(), nullptr));
		}
		THROW_IF_FAILED(document->putref_documentElement(rootElement.get()));

		return document;
	}
}

void sfh::ConfigFile::WriteToFile(const std::wstring& path, const ConfigFile& config)
{
	auto com = wil::CoInitializeEx();

	wil::com_ptr<IStream> stream;
	THROW_IF_FAILED(
		SHCreateStreamOnFileEx(
			path.c_str(),
			STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
			FILE_ATTRIBUTE_NORMAL,
			TRUE,
			nullptr,
			stream.put()));

	auto document = ConfigFileToDocument(config);
	WriteDocumentToFile(stream, document);
}

void sfh::FontDatabase::WriteToFile(const std::wstring& path, const FontDatabase& db)
{
	auto com = wil::CoInitializeEx();

	wil::com_ptr<IStream> stream;
	THROW_IF_FAILED(
		SHCreateStreamOnFileEx(
			path.c_str(),
			STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
			FILE_ATTRIBUTE_NORMAL,
			TRUE,
			nullptr,
			stream.put()));

	auto document = FontDatabaseToDocument(db);
	WriteDocumentToFile(stream, document);
}
