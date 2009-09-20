#include "document.h"
#include "node.h"
#include "element.h"

#include <libxml/xmlstring.h>

using namespace v8;
using namespace libxmljs;

#define VERSION_SYMBOL  String::NewSymbol("version")

#define UNWRAP_DOCUMENT(from)                               \
  Document *document = ObjectWrap::Unwrap<Document>(from);  \
  assert(document);

#define BUILD_NODE(type, name, node)                            \
{                                                               \
  type *name = new type(node);                                  \
  Persistent<Object> name##JS = Persistent<Object>::New(        \
    type::constructor_template->GetFunction()->NewInstance());  \
  node->_private = *name##JS;                                   \
  name->Wrap(name##JS);                                         \
}

namespace
{

//Called by libxml whenever it constructs something,
//such as a node or attribute.
//This allows us to create a C++ instance for every C instance.
void on_libxml_construct(xmlNode* node)
{
  switch (node->type) {
    case XML_DOCUMENT_NODE:
      {
        Document *doc = new Document(node->doc);
        Handle<Value> argv[1] = { Null() };
        Persistent<Object> jsDocument = Persistent<Object>::New(
          Document::constructor_template->GetFunction()->NewInstance(1, argv));
        node->_private = *jsDocument;
        doc->Wrap(jsDocument);
      }
      break;
      
    case XML_ELEMENT_NODE:
      BUILD_NODE(Element, elem, node);
      break;

  }
}

} // namespace

Document::Init::Init()
{
  xmlInitParser(); //Not always necessary, but necessary for thread safety.
  xmlRegisterNodeDefault(on_libxml_construct);
  // xmlDeregisterNodeDefault(on_libxml_destruct);
  xmlThrDefRegisterNodeDefault(on_libxml_construct);
  // xmlThrDefDeregisterNodeDefault(on_libxml_destruct);
}

Document::Init::~Init()
{
  xmlCleanupParser(); //As per xmlInitParser(), or memory leak will happen.
}

Document::Init Document::init_;


Persistent<FunctionTemplate> Document::constructor_template;

Handle<Value>
Document::GetProperty(
  Local<String> property,
  const AccessorInfo& info)
{
  HandleScope scope;
  UNWRAP_DOCUMENT(info.This());

  if (property == VERSION_SYMBOL)
    return document->get_version();

  else if (property == DOCUMENT_SYMBOL)
    return info.This();

  return Undefined();
}

Handle<Value>
Document::Encoding(
  const Arguments& args)
{
  HandleScope scope;
  UNWRAP_DOCUMENT(args.This());

  if (args.Length() == 0)
    return document->get_encoding();

  String::Utf8Value encoding(args[0]->ToString());
  document->set_encoding(*encoding);
  return args.This();
}

Handle<Value>
Document::Root(
  const Arguments& args)
{
  HandleScope scope;
  UNWRAP_DOCUMENT(args.This());

  if (args.Length() == 0)
    return document->get_root();

  if (document->has_root())
    return ThrowException(Exception::Error(String::New("This document already has a root node")));

  Element *element = ObjectWrap::Unwrap<Element>(args[0]->ToObject());
  assert(element);
  document->set_root(element->xml_obj);
  return args[0];
}

Handle<Value>
Document::ToString(
  const Arguments& args)
{
  HandleScope scope;
  UNWRAP_DOCUMENT(args.This());
  return document->to_string();
}


Handle<Value>
Document::New(
 const Arguments& args)
{
  HandleScope scope;

  Handle<Function> callback;
  String::Utf8Value *version = NULL, *encoding = NULL;

  switch (args.Length()) {
    case 0: // newDocument()
      break;

    case 1: // newDocument(version), newDocument(callback)
      if (args[0]->IsNull())
        return args.This();

      if (args[0]->IsString()) {
        version = new String::Utf8Value(args[0]->ToString());

      } else if (args[0]->IsFunction()) {
        callback = Handle<Function>::Cast(args[0]);

      } else {
        LIBXMLJS_THROW_EXCEPTION("Bad argument: newDocument([version]) or newDocument([callback])");

      }
      break;

    case 2: // newDocument(version, encoding), newDocument(version, callback)
      if (args[0]->IsString() && args[1]->IsString()) {
        version = new String::Utf8Value(args[0]->ToString());
        encoding = new String::Utf8Value(args[1]->ToString());

      } else if (args[0]->IsString() && args[1]->IsFunction()) {
        version = new String::Utf8Value(args[0]->ToString());
        callback = Handle<Function>::Cast(args[1]);

      } else {
        LIBXMLJS_THROW_EXCEPTION("Bad argument: newDocument([version], [encoding]) or newDocument([version], [callback])");

      }
      break;

    default: // newDocument(version, encoding, callback)
      if (args[0]->IsString() && args[1]->IsString() && args[2]->IsFunction()) {
        version = new String::Utf8Value(args[0]->ToString());
        encoding = new String::Utf8Value(args[1]->ToString());
        callback = Handle<Function>::Cast(args[2]);

      } else {
        LIBXMLJS_THROW_EXCEPTION("Bad argument: newDocument([version], [encoding], [callback])");

      }
      break;
  }

  if (!version)
    version = new String::Utf8Value(String::New("1.0"));

  xmlDoc * doc = xmlNewDoc((const xmlChar*)**version);
  Persistent<Object> obj = Persistent<Object>((Object*)doc->_private);
  Document *document = ObjectWrap::Unwrap<Document>(obj);

  if (encoding)
    document->set_encoding(**encoding);

  if (*callback && !callback->IsNull()) {
    Handle<Value> argv[1] = { obj };
    *callback->Call(obj, 1, argv);
  }

  return obj;
}

Document::~Document()
{
  xmlFreeDoc(xml_obj);
}

void
Document::set_encoding(
  const char * encoding)
{
  xml_obj->encoding = (const xmlChar*)encoding;
}

Handle<Value>
Document::get_encoding()
{
  if(xml_obj->encoding)
    return String::New((const char *)xml_obj->encoding, xmlStrlen((const xmlChar*)xml_obj->encoding));

  return Null();
}

Handle<Value>
Document::get_version()
{
  if(xml_obj->version)
    return String::New((const char *)xml_obj->version, xmlStrlen((const xmlChar*)xml_obj->version));

  return Null();
}

Handle<Value>
Document::to_string()
{
  xmlChar* buffer = 0;
  int len = 0;

  xmlDocDumpFormatMemoryEnc(xml_obj, &buffer, &len, "UTF-8", 0);
  Handle<String> str = String::New((const char*)buffer, len);
  xmlFree(buffer);

  return str;
}

bool
Document::has_root()
{
  return xmlDocGetRootElement(xml_obj) != NULL;
}

Handle<Value>
Document::get_root()
{
  xmlNodePtr root = xmlDocGetRootElement(xml_obj);
  if (root)
    return Persistent<Object>((Object*)root->_private);
  else
    return Null();
}

void
Document::set_root(
  xmlNodePtr node)
{
  xmlDocSetRootElement(xml_obj, node);
}

void
Document::Initialize (
  Handle<Object> target)
{
  HandleScope scope;
  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  constructor_template = Persistent<FunctionTemplate>::New(t);
  constructor_template->InstanceTemplate()->SetInternalFieldCount(1);

  LIBXMLJS_SET_PROTOTYPE_METHOD(constructor_template, "root", Document::Root);
  LIBXMLJS_SET_PROTOTYPE_METHOD(constructor_template, "encoding", Document::Encoding);

  constructor_template->PrototypeTemplate()->SetAccessor(DOCUMENT_SYMBOL, Document::GetProperty);
  constructor_template->PrototypeTemplate()->SetAccessor(VERSION_SYMBOL, Document::GetProperty);

  LIBXMLJS_SET_PROTOTYPE_METHOD(constructor_template, "toString", Document::ToString);

  target->Set(String::NewSymbol("Document"), constructor_template->GetFunction());

  Node::Initialize(target);
}