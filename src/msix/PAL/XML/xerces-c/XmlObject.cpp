//
//  Copyright (C) 2017 Microsoft.  All rights reserved.
//  See LICENSE file in the project root for full license information.
// 
#include <memory>
#include <string>
#include <vector>
#include <map>

#include "Exceptions.hpp"
#include "StreamBase.hpp"
#include "IXml.hpp"
#include "StreamHelper.hpp"
#include "MSIXResource.hpp"
#include "UnicodeConversion.hpp"
#include "Enumerators.hpp"

// Mandatory for using any feature of Xerces.
#include "xercesc/dom/DOM.hpp"
#include "xercesc/framework/MemBufInputSource.hpp"
#include "xercesc/framework/XMLGrammarPoolImpl.hpp"
#include "xercesc/parsers/AbstractDOMParser.hpp"
#include "xercesc/parsers/XercesDOMParser.hpp"
#include "xercesc/sax/ErrorHandler.hpp"
#include "xercesc/util/PlatformUtils.hpp"
#include "xercesc/util/XMLString.hpp"
#include "xercesc/util/Base64.hpp"

XERCES_CPP_NAMESPACE_USE

EXTERN_C const IID IID_IXercesElement;

#ifndef WIN32
// {07d6ee0e-2165-4b90-8024-e176291e77dd}
interface IXercesElement : public IUnknown
#else
#include "Unknwn.h"
#include "Objidl.h"
class IXercesElement : public IUnknown
#endif
// An internal interface for XML document object model
{
public:
    virtual DOMElement* GetElement() = 0;
};

SpecializeUuidOfImpl(IXercesElement);

namespace MSIX {

static std::map<XmlQueryName, std::string> xPaths = {
    {XmlQueryName::Package_Identity                               ,"/Package/Identity"},
    {XmlQueryName::BlockMap_File                                  ,"/BlockMap/File"},
    {XmlQueryName::BlockMap_File_Block                            ,"./Block"},
    {XmlQueryName::Bundle_Identity                                ,"/Bundle/Identity"},
    {XmlQueryName::Bundle_Packages_Package                        ,"/Bundle/Packages/Package"},
    {XmlQueryName::Bundle_Packages_Package_Resources_Resource     ,"./Resources/Resource"},
    {XmlQueryName::Package_Dependencies_TargetDeviceFamily        ,"/Package/Dependencies/TargetDeviceFamily"},
    {XmlQueryName::Package_Applications_Application               ,"/Package/Applications/Application"},
    {XmlQueryName::Package_Properties                             ,"/Package/Properties"},
    {XmlQueryName::Package_Properties_Description                 ,"./Description"},
    {XmlQueryName::Package_Properties_DisplayName                 ,"./DisplayName"},
    {XmlQueryName::Package_Properties_PublisherDisplayName        ,"./PublisherDisplayName"},
    {XmlQueryName::Package_Properties_Logo                        ,"./Logo"},
    {XmlQueryName::Package_Properties_Framework                   ,"./Framework"},
    {XmlQueryName::Package_Properties_ResourcePackage             ,"./ResourcePackage"},
    {XmlQueryName::Package_Properties_AllowExecution              ,"./AllowExecution"},
    {XmlQueryName::Package_Dependencies_PackageDependency         ,"/Package/Dependencies/PackageDependency"},
    {XmlQueryName::Package_Capabilities_Capability                ,"/Package/Capabilities/Capability"},
    {XmlQueryName::Package_Resources_Resource                     ,"/Package/Resources/Resource"},
};

static std::map<XmlAttributeName, std::string> attributeNames = {
    {XmlAttributeName::Name                                       ,"Name"},
    {XmlAttributeName::ResourceId                                 ,"ResourceId"},
    {XmlAttributeName::Version                                    ,"Version"},
    {XmlAttributeName::Size                                       ,"Size"},
    {XmlAttributeName::Identity_ProcessorArchitecture             ,"ProcessorArchitecture"},
    {XmlAttributeName::Publisher                                  ,"Publisher"},
    {XmlAttributeName::BlockMap_File_LocalFileHeaderSize          ,"LfhSize"},    
    {XmlAttributeName::BlockMap_File_Block_Hash                   ,"Hash"},
    {XmlAttributeName::Bundle_Package_FileName                    ,"FileName"},
    {XmlAttributeName::Bundle_Package_Offset                      ,"Offset"},
    {XmlAttributeName::Bundle_Package_Type                        ,"Type"},
    {XmlAttributeName::Bundle_Package_Architecture                ,"Architecture"},
    {XmlAttributeName::Language                                   ,"Language"},
    {XmlAttributeName::MinVersion                                 ,"MinVersion"},
    {XmlAttributeName::Dependencies_Tdf_MaxVersionTested          ,"MaxVersionTested"},
    {XmlAttributeName::Package_Applications_Application_Id        ,"Id"},
};

class ParsingException final : public XERCES_CPP_NAMESPACE::ErrorHandler
{
public:
    ParsingException() {};
    ~ParsingException() {};

    void warning(const XERCES_CPP_NAMESPACE::SAXParseException& exp) override
    {        
        ThrowError(MSIX::Error::XmlWarning);
    }

    void error(const XERCES_CPP_NAMESPACE::SAXParseException& exp) override
    {
        ThrowError(MSIX::Error::XmlError);
    }

    void fatalError(const XERCES_CPP_NAMESPACE::SAXParseException& exp) override
    {
        ThrowError(MSIX::Error::XmlFatal);
    }

    void resetErrors() override {}
};

template<class T>
class XercesPtr
{
public:
    XercesPtr() : m_ptr(nullptr) {}
    XercesPtr(T* p)  : m_ptr(p) {}

    // move ctor
    XercesPtr(XercesPtr &&right) : m_ptr(nullptr)
    {
        if (this != reinterpret_cast<XercesPtr*>(&reinterpret_cast<std::int8_t&>(right)))
        {   Swap(right);
        }
    } 

    virtual ~XercesPtr() { InternalRelease(); }

    void InternalRelease()
    {
        T* temp = m_ptr;
        if (temp)
        {
            m_ptr = nullptr;
            temp->release();
        }
    }
    
    XercesPtr& operator=(XercesPtr&& right)
    {   XercesPtr(std::move(right)).Swap(*this);
        return *this;
    }

    T* operator->() const { return m_ptr; }
    T* Get() const { return m_ptr; }
protected:
    inline void Swap(XercesPtr& right ) { std::swap(m_ptr, right.m_ptr); }
    T* m_ptr = nullptr;
};

class XercesCharPtr
{
public:
    XercesCharPtr(char* c) : m_ptr(c) {};
    ~XercesCharPtr() { InternalRelease(); }

    // move ctor
    XercesCharPtr(XercesCharPtr &&right) : m_ptr(nullptr)
    {
        if (this != reinterpret_cast<XercesCharPtr*>(&reinterpret_cast<std::int8_t&>(right)))
        {   Swap(right);
        }
    } 

    void InternalRelease()
    {   XERCES_CPP_NAMESPACE::XMLString::release(&m_ptr);
        m_ptr = nullptr;
    }

    XercesCharPtr& operator=(XercesCharPtr&& right)
    {   XercesCharPtr(std::move(right)).Swap(*this);
        return *this;
    }

    char* operator->() const { return m_ptr; }
    char* Get() const { return m_ptr; }
protected:
    inline void Swap(XercesCharPtr& right ) { std::swap(m_ptr, right.m_ptr); }
    char* m_ptr = nullptr;        
};

class XercesXMLChPtr
{
public:
    XercesXMLChPtr(XMLCh* c) : m_ptr(c) {}
    ~XercesXMLChPtr() { InternalRelease(); }

    // move ctor
    XercesXMLChPtr(XercesXMLChPtr &&right) : m_ptr(nullptr)
    {
        if (this != reinterpret_cast<XercesXMLChPtr*>(&reinterpret_cast<std::int8_t&>(right)))
        {   Swap(right);
        }
    }

    void InternalRelease()
    {   XERCES_CPP_NAMESPACE::XMLString::release(&m_ptr);
        m_ptr = nullptr;
    }

    XercesXMLChPtr& operator=(XercesXMLChPtr&& right)
    {   XercesXMLChPtr(std::move(right)).Swap(*this);
        return *this;
    }

    XMLCh* operator->() const { return m_ptr; }
    XMLCh* Get() const { return m_ptr; }
protected:
    inline void Swap(XercesXMLChPtr& right ) { std::swap(m_ptr, right.m_ptr); }
    XMLCh* m_ptr = nullptr;              
};

class XercesXMLBytePtr
{
public:
    XercesXMLBytePtr(XMLByte* c) : m_ptr(c) {}
    ~XercesXMLBytePtr() { InternalRelease(); }

    // move ctor
    XercesXMLBytePtr(XercesXMLBytePtr &&right) : m_ptr(nullptr)
    {
        if (this != reinterpret_cast<XercesXMLBytePtr*>(&reinterpret_cast<std::int8_t&>(right)))
        {   Swap(right);
        }
    }  

    void InternalRelease()
    {   delete(m_ptr);
        m_ptr = nullptr;
    }

    XercesXMLBytePtr& operator=(XercesXMLBytePtr&& right)
    {   XercesXMLBytePtr(std::move(right)).Swap(*this);
        return *this;
    }

    XMLByte* operator->() const { return m_ptr; }
    XMLByte* Get() const { return m_ptr; }
protected:
    inline void Swap(XercesXMLBytePtr& right ) { std::swap(m_ptr, right.m_ptr); }
    XMLByte* m_ptr = nullptr;             
};    

class XercesElement final : public ComClass<XercesElement, IXmlElement, IXercesElement, IMsixElement>
{
private:
    std::string GetAttributeValue(std::string& attributeName)
    {
        XercesXMLChPtr nameAttr(XMLString::transcode(attributeName.c_str()));
        XercesCharPtr value(XMLString::transcode(m_element->getAttribute(nameAttr.Get())));
        return std::string(value.Get());
    }

public:
    XercesElement(IMsixFactory* factory, DOMElement* element, XERCES_CPP_NAMESPACE::XercesDOMParser* parser) :
        m_factory(factory), m_element(element), m_parser(parser)
    {
        m_resolver = XercesPtr<DOMXPathNSResolver>(m_parser->getDocument()->createNSResolver(m_parser->getDocument()));
    }
    
    // IXmlElement
    std::string GetAttributeValue(XmlAttributeName attribute) override
    {
        return GetAttributeValue(attributeNames[attribute]);
    }

    std::vector<std::uint8_t> GetBase64DecodedAttributeValue(XmlAttributeName attribute) override
    {
        XercesXMLChPtr nameAttr(XMLString::transcode(attributeNames[attribute].c_str()));
        XMLSize_t len = 0;
        XercesXMLBytePtr decodedData(XERCES_CPP_NAMESPACE::Base64::decodeToXMLByte(
            m_element->getAttribute(nameAttr.Get()),
            &len));
        std::vector<std::uint8_t> result(len);
        for(XMLSize_t index=0; index < len; index++)
        {   result[index] = static_cast<std::uint8_t>(decodedData.Get()[index]);
        }
        return result;
    }

    std::string GetText() override
    {
        DOMNode* node = dynamic_cast<DOMNode*>(m_element);
        XercesCharPtr value(XMLString::transcode(node->getTextContent()));
        return std::string(value.Get());
    }

    // IXercesElement
    DOMElement* GetElement() override { return m_element; }

     // IMsixElement
    HRESULT STDMETHODCALLTYPE GetAttributeValue(LPCWSTR name, LPWSTR* value) noexcept override try
    {
        ThrowErrorIf(Error::InvalidParameter, (value == nullptr), "bad pointer.");
        auto intermediate = utf16_to_utf8(name);
        auto attributeValue = GetAttributeValue(intermediate);
        return m_factory->MarshalOutString(attributeValue, value);
    } CATCH_RETURN();

    HRESULT STDMETHODCALLTYPE GetText(LPWSTR* value) noexcept override try
    {
        ThrowErrorIf(Error::InvalidParameter, (value == nullptr), "bad pointer.");
        auto text = GetText();
        return m_factory->MarshalOutString(text, value);
    } CATCH_RETURN();

    HRESULT STDMETHODCALLTYPE GetElements(LPCWSTR name, IMsixElementEnumerator** elements) noexcept override try
    {
        ThrowErrorIf(Error::InvalidParameter, (elements == nullptr || *elements != nullptr), "bad pointer.");

        // Note: getElementsByTagName only returns the childs of a DOMElement and doesn't 
        // support xPath. For this reason we need the XercesDomParser in this object.
        auto intermediate = utf16_to_utf8(name);
        XercesXMLChPtr xPath(XMLString::transcode(intermediate.c_str()));
        XercesPtr<DOMXPathResult> result(m_parser->getDocument()->evaluate(
            xPath.Get(),
            m_element,
            m_resolver.Get(),
            DOMXPathResult::ORDERED_NODE_SNAPSHOT_TYPE,
            nullptr));

        std::vector<ComPtr<IMsixElement>> elementsEnum;
        for (XMLSize_t i = 0; i < result->getSnapshotLength(); i++)
        {
            result->snapshotItem(i);
            auto node = static_cast<DOMElement*>(result->getNodeValue());
            auto item = ComPtr<IMsixElement>::Make<XercesElement>(m_factory, node, m_parser);
            elementsEnum.push_back(std::move(item));
        }
        *elements = ComPtr<IMsixElementEnumerator>::
            Make<EnumeratorCom<IMsixElementEnumerator,IMsixElement>>(elementsEnum).Detach();
        return static_cast<HRESULT>(Error::OK);
    } CATCH_RETURN();

protected:
    IMsixFactory* m_factory = nullptr;
    DOMElement* m_element = nullptr;
    XERCES_CPP_NAMESPACE::XercesDOMParser* m_parser;
    XercesPtr<DOMXPathNSResolver> m_resolver;
};

class XercesDom final : public ComClass<XercesDom, IXmlDom>
{
public:
    XercesDom(IMsixFactory* factory, const ComPtr<IStream>& stream, std::vector<ComPtr<IStream>>* schemas = nullptr) :
        m_factory(factory), m_stream(stream)
    {
        auto buffer = Helper::CreateBufferFromStream(stream);
        std::unique_ptr<XERCES_CPP_NAMESPACE::MemBufInputSource> source = std::make_unique<XERCES_CPP_NAMESPACE::MemBufInputSource>(
            reinterpret_cast<const XMLByte*>(&buffer[0]), buffer.size(), "XML File");

        // Create parser and grammar pool
        auto grammarPool = std::make_unique<XERCES_CPP_NAMESPACE::XMLGrammarPoolImpl>(XERCES_CPP_NAMESPACE::XMLPlatformUtils::fgMemoryManager);
        m_parser = std::make_unique<XERCES_CPP_NAMESPACE::XercesDOMParser>(nullptr, XERCES_CPP_NAMESPACE::XMLPlatformUtils::fgMemoryManager, grammarPool.get());
        
        // For Non validation parser GetResources will return an empty vector for the ContentType, BlockMap and AppxBundleManifest.
        // XercesDom will only parse the schemas if the vector is not empty. If not, it will only see that it is valid xml.
        bool HasSchemas = ((schemas != nullptr) && (schemas->begin() != schemas->end()));
        m_parser->setValidationScheme(HasSchemas ? 
            XERCES_CPP_NAMESPACE::AbstractDOMParser::ValSchemes::Val_Always : 
            XERCES_CPP_NAMESPACE::AbstractDOMParser::ValSchemes::Val_Never
        );
        m_parser->cacheGrammarFromParse(HasSchemas);            
        m_parser->setDoSchema(HasSchemas);
        m_parser->setDoNamespaces(HasSchemas);
        m_parser->setHandleMultipleImports(HasSchemas); // TODO: do we need to handle the case where there aren't multiple schemas with the same namespace?
        m_parser->setValidationSchemaFullChecking(HasSchemas);

        if (HasSchemas)
        {   // Disable DTD and prevent XXE attacks.  See https://www.owasp.org/index.php/XML_External_Entity_(XXE)_Prevention_Cheat_Sheet#libxerces-c for additional details.
            m_parser->setIgnoreCachedDTD(true);
            m_parser->setSkipDTDValidation(true);
            m_parser->setCreateEntityReferenceNodes(false);
        }

        // Add schemas
        if (HasSchemas)
        {   for(auto& schema : *schemas)
            {   auto schemaBuffer = Helper::CreateBufferFromStream(schema);
                auto item = std::make_unique<XERCES_CPP_NAMESPACE::MemBufInputSource>(
                    reinterpret_cast<const XMLByte*>(&schemaBuffer[0]), schemaBuffer.size(), "Schema");
                m_parser->loadGrammar(*item, XERCES_CPP_NAMESPACE::Grammar::GrammarType::SchemaGrammarType, true);
            }           
        }

        // Set the error handler for the parser
        auto errorHandler = std::make_unique<ParsingException>();
        m_parser->setErrorHandler(errorHandler.get());
        m_parser->parse(*source);

        m_resolver = XercesPtr<DOMXPathNSResolver>(m_parser->getDocument()->createNSResolver(m_parser->getDocument()));
    }

    // IXmlDom
    MSIX::ComPtr<IXmlElement> GetDocument() override
    {
        return ComPtr<IXmlElement>::Make<XercesElement>(m_factory, m_parser->getDocument()->getDocumentElement(), m_parser.get());
    }

    bool ForEachElementIn(const ComPtr<IXmlElement>& root, XmlQueryName query, XmlVisitor& visitor) override
    {
        ComPtr<IXercesElement> element;
        ThrowHrIfFailed(root->QueryInterface(UuidOfImpl<IXercesElement>::iid, reinterpret_cast<void**>(&element)));

        XercesXMLChPtr xPath(XMLString::transcode(xPaths[query].c_str()));
        XercesPtr<DOMXPathResult> result(m_parser->getDocument()->evaluate(
            xPath.Get(),
            element->GetElement(),
            m_resolver.Get(),
            DOMXPathResult::ORDERED_NODE_SNAPSHOT_TYPE,
            nullptr));
        
        for (XMLSize_t i = 0; i < result->getSnapshotLength(); i++)
        {
            result->snapshotItem(i);
            auto node = static_cast<DOMElement*>(result->getNodeValue());
            auto item = ComPtr<IXmlElement>::Make<XercesElement>(m_factory, node, m_parser.get());
            if (!visitor.Callback(visitor.context, item))
            {
                return false;
            }
        }
        return true;
    }

protected:
    IMsixFactory* m_factory;
    std::unique_ptr<XERCES_CPP_NAMESPACE::XercesDOMParser> m_parser;
    XercesPtr<DOMXPathNSResolver> m_resolver;
    ComPtr<IStream> m_stream;
};

class XercesFactory final : public ComClass<XercesFactory, IXmlFactory>
{
public:
    XercesFactory(IMsixFactory* factory) : m_factory(factory)
    {
        XERCES_CPP_NAMESPACE::XMLPlatformUtils::Initialize();
    }

    ~XercesFactory()
    {
        XERCES_CPP_NAMESPACE::XMLPlatformUtils::Terminate();
    }

    ComPtr<IXmlDom> CreateDomFromStream(XmlContentType footPrintType, const ComPtr<IStream>& stream) override
    {
        switch (footPrintType)
        {
            case XmlContentType::AppxBlockMapXml:
            {   auto blockMapSchema = GetResources(m_factory, Resource::Type::BlockMap);
                return ComPtr<IXmlDom>::Make<XercesDom>(m_factory, stream, &blockMapSchema);
            }
            case XmlContentType::AppxManifestXml:
                // TODO: pass schemas to validate AppxManifest. This only validates that is a well-formed xml
                return ComPtr<IXmlDom>::Make<XercesDom>(m_factory, stream);
            case XmlContentType::ContentTypeXml:
            {   auto contentTypeSchema = GetResources(m_factory, Resource::Type::ContentType);
                return ComPtr<IXmlDom>::Make<XercesDom>(m_factory, stream, &contentTypeSchema);
            }
            case XmlContentType::AppxBundleManifestXml:
            {   // TODO: pass schemas to validate AppxManifest. This only validates that is a well-formed xml
                return ComPtr<IXmlDom>::Make<XercesDom>(m_factory, stream);
            }
        }
        ThrowError(Error::InvalidParameter);
    }
protected:
    IMsixFactory* m_factory;
};

ComPtr<IXmlFactory> CreateXmlFactory(IMsixFactory* factory) { return ComPtr<IXmlFactory>::Make<XercesFactory>(factory); }

} // namespace MSIX