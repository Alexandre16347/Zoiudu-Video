// =================================================================================
// 26. CONFIGURAÇÕES DE REDE (WIFI)
// =================================================================================

#define SECRET_SSID "Seu Wifi"
#define SECRET_PASS "Sua Senha"

// =================================================================================
// 27. CREDENCIAIS DO TELEGRAM API
// =================================================================================

// Token: 
#define SECRET_BOTtoken "Token do Seu Bot"

// =================================================================================
// 28. GESTÃO DE ACESSO E PERMISSÕES
// =================================================================================
// ID do Mestre (Administrador do Zoiudo)
#define chat_id_master "Seu ID" 

// Listas de usuários permitidos (Máximo 5 slots)
String Nomes[]   = {"Nome", "", "", "", ""};
String CHAT_ID[] = {"ID", "", "", "", ""};

// =================================================================================
// 29. MACROS E UTILITÁRIOS DE COMPILAÇÃO
// =================================================================================
// Calcula automaticamente o tamanho dos arrays de usuários
#define NUMITEMS(arg) ((unsigned int) (sizeof (arg) / sizeof (arg [0])))
