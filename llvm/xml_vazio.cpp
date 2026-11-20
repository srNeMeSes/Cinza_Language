// Sem o libxml2 dentro do cinzac (Q5, só no Windows). O LLVMWindowsManifest do MSYS2 (que o lld
// COFF usa para juntar manifestos do Windows) foi compilado para o libxml2 como DLL: cada chamada
// lê o ponteiro __imp_<nome>. O driver MinGW, o único que o cinzac usa, nunca junta manifestos, então
// esses ponteiros apontam para funções vazias — e o libxml2, o libiconv (LGPL) e o liblzma ficam fora
// do executável, que leva só código de licença permissiva. Se um dia o código do manifesto rodar,
// xmlReadMemory devolve nulo e o LLVM relata a falha (não há queda).
//
// Chamar uma função sem parâmetros com argumentos é seguro na convenção do x64 (quem chama limpa a
// pilha), e o valor devolvido (nulo, ou 0) é ignorado nas que não devolvem nada.

namespace {
void* nada() { return nullptr; }
void  liberaNada(void*) {}
void (*liberaPtr)(void*) = liberaNada;   // xmlFree é uma variável (um ponteiro de função)
}  // namespace

#define CINZA_XML_VAZIO(nome) void* __imp_##nome = reinterpret_cast<void*>(&nada);

extern "C" {
CINZA_XML_VAZIO(xmlAddChild)
CINZA_XML_VAZIO(xmlCopyNamespace)
CINZA_XML_VAZIO(xmlDocDumpFormatMemoryEnc)
CINZA_XML_VAZIO(xmlDocGetRootElement)
CINZA_XML_VAZIO(xmlDocSetRootElement)
CINZA_XML_VAZIO(xmlFreeDoc)
CINZA_XML_VAZIO(xmlFreeNode)
CINZA_XML_VAZIO(xmlFreeNs)
CINZA_XML_VAZIO(xmlNewDoc)
CINZA_XML_VAZIO(xmlNewNs)
CINZA_XML_VAZIO(xmlNewProp)
CINZA_XML_VAZIO(xmlReadMemory)
CINZA_XML_VAZIO(xmlSetGenericErrorFunc)
CINZA_XML_VAZIO(xmlStrdup)
CINZA_XML_VAZIO(xmlUnlinkNode)
void* __imp_xmlFree = reinterpret_cast<void*>(&liberaPtr);
}
