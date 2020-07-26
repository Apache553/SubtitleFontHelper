
#include "ConfigFile.h"

#include "Common.h"
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

bool MyConfig::ToFile(const std::wstring& filename)const
{
	QByteArray bytes;
	QXmlStreamWriter writer(&bytes);
	writer.setCodec("UTF-8");
	writer.writeStartDocument();
	// root element
	writer.writeStartElement("ConfigFile");
	// index files
	for (const auto& fn : index_files) {
		writer.writeStartElement("IndexFile");
		writer.writeAttribute("path", QString::fromStdWString(fn));
		writer.writeEndElement();
	}
	// root element
	writer.writeEndElement();
	writer.writeEndDocument();
	
	return WriteAllToFile(filename, bytes.toStdString());
}

MyConfig MyConfig::FromFile(const std::wstring& filename)
{
	bool success = false;
	MyConfig conf;
	GetFileMemoryBuffer(filename, [&](void* mem, size_t len, const std::wstring& fn) {
		QByteArray data = QByteArray::fromRawData((char*)mem, len);
		QXmlStreamReader reader(data);
		while (!reader.atEnd()) {
			QXmlStreamReader::TokenType token = reader.readNext();
			if (reader.hasError()) {
				success = false;
				return;
			}
			switch (token) {
			case QXmlStreamReader::StartDocument:
				// ignore
				break;
			case QXmlStreamReader::StartElement:
				if (reader.name() == "IndexFile") {
					auto attr = reader.attributes();
					conf.index_files.push_back(attr.value("path").toString().toStdWString());
				}
				break;
			case QXmlStreamReader::EndElement:
				break;
			case QXmlStreamReader::EndDocument:
				break;
			}
		}
		success = true;
		});
	return conf;
}
