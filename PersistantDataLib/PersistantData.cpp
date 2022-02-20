#include "PersistantData.h"

#include <cassert>
#include <stdexcept>
#include <atomic>

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
	wil::unique_bstr GetNodeName(IXMLDOMNode* node)
	{
		wil::unique_bstr name;
		THROW_IF_FAILED(node->get_nodeName(name.put()));
		return name;
	}

	wil::unique_bstr GetNodeText(IXMLDOMNode* node)
	{
		wil::unique_bstr value;
		if (THROW_IF_FAILED(node->get_text(value.addressof())) != S_OK)
			return {};
		return value;
	}

	wil::unique_bstr GetAttributeText(IXMLDOMNamedNodeMap* map, const wchar_t* name)
	{
		wil::com_ptr<IXMLDOMNode> attribute;
		if (THROW_IF_FAILED(map->getNamedItem(wil::make_bstr(name).get(), attribute.put())) != S_OK)
			return {};
		return GetNodeText(attribute.get());
	}
}

sfh::ConfigFile sfh::ConfigFile::ReadFromFile(const std::wstring& path)
{
	ConfigFile ret;
	assert(SUCCEEDED(CoInitialize(nullptr)));
	auto uninitializeCom = wil::scope_exit([]() { CoUninitialize(); });

	auto document = wil::CoCreateInstance<IXMLDOMDocument>(CLSID_DOMDocument30);

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

	VARIANT_BOOL isSuccessful = VARIANT_FALSE;
	THROW_IF_FAILED(document->load(pathVariant, &isSuccessful));
	if (isSuccessful == VARIANT_FALSE)throw std::runtime_error("failed to load xml");


	wil::com_ptr<IXMLDOMElement> rootNode;
	if (THROW_IF_FAILED(document->get_documentElement(rootNode.put())) == S_FALSE)
		throw std::runtime_error("invalid xml");

	auto rootNodeName = GetNodeName(rootNode.get());
	if (wcscmp(rootNodeName.get(), L"ConfigFile") != 0)
		throw std::runtime_error("invalid config xml");

	// get all child nodes
	wil::com_ptr<IXMLDOMNodeList> childNodes;
	THROW_IF_FAILED(rootNode->get_childNodes(childNodes.put()));

	// traverse all child nodes
	wil::com_ptr<IXMLDOMNode> childNode;
	THROW_IF_FAILED(childNodes->nextNode(childNode.put()));
	while (childNode)
	{
		auto nodeName = GetNodeName(childNode.get());
		if (wcscmp(nodeName.get(), L"IndexFile") == 0)
		{
			// IndexFile
			IndexFileElement item;

			if (auto value = GetNodeText(childNode.get()))
			{
				item.m_path = value.get();
				ret.m_indexFile.emplace_back(std::move(item));
			}
		}
		else if (wcscmp(nodeName.get(), L"MonitorProcess") == 0)
		{
			// MonitorProcess
			MonitorProcessElement item;

			wil::com_ptr<IXMLDOMNamedNodeMap> map;
			THROW_IF_FAILED(childNode->get_attributes(map.put()));

			if (auto value = GetNodeText(childNode.get()))
			{
				item.m_name = value.get();
				ret.m_monitorProcess.emplace_back(std::move(item));
			}
		}
		THROW_IF_FAILED(childNodes->nextNode(childNode.put()));
	}

	return ret;
}

sfh::FontDatabase sfh::FontDatabase::ReadFromFile(const std::wstring& path)
{
	FontDatabase ret;
	assert(SUCCEEDED(CoInitialize(nullptr)));
	auto uninitializeCom = wil::scope_exit([]() { CoUninitialize(); });

	auto document = wil::CoCreateInstance<IXMLDOMDocument>(CLSID_DOMDocument30);

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

	VARIANT_BOOL isSuccessful = VARIANT_FALSE;
	THROW_IF_FAILED(document->load(pathVariant, &isSuccessful));
	if (isSuccessful == VARIANT_FALSE)throw std::runtime_error("failed to load xml");

	wil::com_ptr<IXMLDOMElement> rootNode;
	if (THROW_IF_FAILED(document->get_documentElement(rootNode.put())) == S_FALSE)
		throw std::runtime_error("invalid xml");

	// validate font database root element name
	auto rootNodeName = GetNodeName(rootNode.get());
	if (wcscmp(rootNodeName.get(), L"FontDatabase") != 0)
		throw std::runtime_error("invalid font database xml");

	wil::com_ptr<IXMLDOMNodeList> fontfaceList;
	THROW_IF_FAILED(rootNode->get_childNodes(fontfaceList.put()));

	// reserve space
	long fontfaceCount = 0;
	THROW_IF_FAILED(fontfaceList->get_length(&fontfaceCount));
	ret.m_fonts.reserve(fontfaceCount);

	wil::com_ptr<IXMLDOMNode> fontface;
	THROW_IF_FAILED(fontfaceList->nextNode(fontface.put()));
	for (; fontface; THROW_IF_FAILED(fontfaceList->nextNode(fontface.put())))
	{
		// validate fontface element name
		auto elementName = GetNodeName(fontface.get());
		if (wcscmp(elementName.get(), L"FontFace") == 0)
		{
			// this is a font face
			FontFaceElement item;

			wil::com_ptr<IXMLDOMNamedNodeMap> map;
			THROW_IF_FAILED(fontface->get_attributes(map.put()));

			if (auto value = GetAttributeText(map.get(), L"path"))
			{
				item.m_path = value.get();
			}
			if (auto value = GetAttributeText(map.get(), L"index"))
			{
				item.m_index = std::stoi(value.get());
			}

			// check necessary attributes
			if (item.m_path.empty())
				continue;
			if (item.m_index == std::numeric_limits<uint32_t>::max())
				continue;

			// fetch names
			wil::com_ptr<IXMLDOMNodeList> names;
			THROW_IF_FAILED(fontface->get_childNodes(names.put()));

			// reserve space
			long nameCount;
			THROW_IF_FAILED(names->get_length(&nameCount));
			item.m_names.reserve(nameCount);

			wil::com_ptr<IXMLDOMNode> name;
			THROW_IF_FAILED(names->nextNode(name.put()));
			while (name)
			{
				auto nameName = GetNodeName(name.get());
				if (wcscmp(nameName.get(),
				           FontFaceElement::NameElement::TYPEMAP[
					           FontFaceElement::NameElement::Win32FamilyName]) == 0)
				{
					if (auto text = GetNodeText(name.get()))
					{
						item.m_names.emplace_back(FontFaceElement::NameElement::Win32FamilyName, text.get());
					}
				}
				else if (wcscmp(nameName.get(),
				                FontFaceElement::NameElement::TYPEMAP[
					                FontFaceElement::NameElement::FullName]) == 0)
				{
					if (auto text = GetNodeText(name.get()))
					{
						item.m_names.emplace_back(FontFaceElement::NameElement::FullName, text.get());
					}
				}
				else if (wcscmp(nameName.get(),
				                FontFaceElement::NameElement::TYPEMAP[
					                FontFaceElement::NameElement::PostScriptName]) == 0)
				{
					if (auto text = GetNodeText(name.get()))
					{
						item.m_names.emplace_back(FontFaceElement::NameElement::PostScriptName, text.get());
					}
				}
				THROW_IF_FAILED(names->nextNode(name.put()));
			}

			// add fontface into list
			ret.m_fonts.emplace_back(std::move(item));
		}
	}
	return ret;
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
	assert(SUCCEEDED(CoInitialize(nullptr)));
	auto uninitializeCom = wil::scope_exit([]() { CoUninitialize(); });

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
	assert(SUCCEEDED(CoInitialize(nullptr)));
	auto uninitializeCom = wil::scope_exit([]() { CoUninitialize(); });

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
