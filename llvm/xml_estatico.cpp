// O libxml2 estático no lugar da DLL (Q5, só no Windows). O LLVMWindowsManifest do MSYS2 (que o
// lld COFF usa para juntar manifestos; o driver MinGW nunca pede isso) foi compilado para o
// libxml2 como DLL: cada chamada lê o ponteiro __imp_<nome>. Aqui esses ponteiros apontam para
// as funções do libxml2.a, e o cinzac continua um executável só, sem a libxml2-16.dll.
#define LIBXML_STATIC
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlmemory.h>

extern "C" {
void* __imp_xmlAddChild               = reinterpret_cast<void*>(&xmlAddChild);
void* __imp_xmlCopyNamespace          = reinterpret_cast<void*>(&xmlCopyNamespace);
void* __imp_xmlDocDumpFormatMemoryEnc = reinterpret_cast<void*>(&xmlDocDumpFormatMemoryEnc);
void* __imp_xmlDocGetRootElement      = reinterpret_cast<void*>(&xmlDocGetRootElement);
void* __imp_xmlDocSetRootElement      = reinterpret_cast<void*>(&xmlDocSetRootElement);
void* __imp_xmlFree                   = reinterpret_cast<void*>(&xmlFree);   // uma variável
void* __imp_xmlFreeDoc                = reinterpret_cast<void*>(&xmlFreeDoc);
void* __imp_xmlFreeNode               = reinterpret_cast<void*>(&xmlFreeNode);
void* __imp_xmlFreeNs                 = reinterpret_cast<void*>(&xmlFreeNs);
void* __imp_xmlNewDoc                 = reinterpret_cast<void*>(&xmlNewDoc);
void* __imp_xmlNewNs                  = reinterpret_cast<void*>(&xmlNewNs);
void* __imp_xmlNewProp                = reinterpret_cast<void*>(&xmlNewProp);
void* __imp_xmlReadMemory             = reinterpret_cast<void*>(&xmlReadMemory);
void* __imp_xmlSetGenericErrorFunc    = reinterpret_cast<void*>(&xmlSetGenericErrorFunc);
void* __imp_xmlStrdup                 = reinterpret_cast<void*>(&xmlStrdup);
void* __imp_xmlUnlinkNode             = reinterpret_cast<void*>(&xmlUnlinkNode);
}
