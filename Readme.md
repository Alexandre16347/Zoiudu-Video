# 👁️ Zoiudo Video 2.0

**Autor:** Alexandre Nery  
**Data de Atualização:** Fevereiro de 2026  
**Versão Base:** 8.9x (James Zahary - [ESP32-CAM-Video-Telegram](https://github.com/jameszah/ESP32-CAM-Video-Telegram))

## 📝 Descrição
O **Zoiudo Video 2.0** é um sistema de monitoramento e captura de mídia baseado no hardware **ESP32-CAM**. O firmware utiliza a PSRAM do módulo para gravar vídeos em formato AVI e enviá-los via Telegram, além de gerenciar permissões de usuários e conectividade de rede de forma dinâmica.

## 🚀 Funcionalidades Técnicas
- **WiFi Manager:** Portal de configuração via Access Point (AP) para troca de rede sem necessidade de reprogramação.
- **Gestão de Acesso:** Lista controlada de até 5 IDs permitidos no Telegram com comandos de inclusão/exclusão.
- **Log de Segurança:** Notificação automática ao Administrador (Mestre) sobre interações de terceiros.
- **Gravação em PSRAM:** Buffer de 3MB para armazenamento temporário de vídeo (150 frames) em formato MJPEG AVI.
- **Sincronização NTP:** Carimbo de data e hora preciso em todas as capturas.
- **Controle de Periféricos:** Acionamento remoto do Flash LED e status de sinal (RSSI).

## 🛠️ Hardware e Requisitos
- Módulo ESP32-CAM (AI-Thinker).
- Fonte de alimentação estável de 5V @ 2A.
- Módulo com PSRAM habilitada no ambiente de desenvolvimento.

## 📚 Bibliotecas Utilizadas
- `UniversalTelegramBot` (Versão Customizada v8.9x)
- `ArduinoJson` (v6.x)
- `WiFiManager`
- `NTPClient`
- `Crescer` (Timer sem delay)

## 🤖 Tabela de Comandos
| Comando | Descrição |
| :--- | :--- |
| `/start` | Menu de ajuda e lista de comandos. |
| `/caption` | Captura de foto com data/hora na legenda. |
| `/clip` | Gravação e envio de vídeo curto (AVI). |
| `/status` | Diagnóstico: IP, RSSI, Memória, Flash e Usuários. |
| `/flash` | Alterna o estado do LED Flash (On/Off). |
| `/add {ID} {Nome}` | Adiciona um novo usuário à lista de permissão. |
| `/remove {Índice}` | Remove um usuário pelo índice da lista. |
| `/time {minutos}` | Configura o timer de fotos automáticas (0 desativa). |
| `/type` | Muda o modo de captura (Flash automático ou manual). |
| `/reboot` | Reinicialização remota do hardware. |

## ⚖️ Licença
Distribuído sob a licença **GNU General Public License v3.0**.















📘 Documentação Técnica.
1. Arquitetura de Memória (PSRAM)
O sistema utiliza a memória RAM externa (PSRAM) para evitar o uso de cartões SD, o que aumenta a velocidade de escrita e reduz falhas mecânicas.

Buffer AVI: Alocação de 3.072.000 bytes.

Buffer de Índice: Alocação de 204.800 bytes.

A gravação é monitorada em tempo real; caso o buffer atinja 95% de sua capacidade, o fechamento do arquivo AVI é forçado para evitar estouro de memória (stack overflow).

2. Processamento Multicore (FreeRTOS)
Para garantir que o envio de vídeos pesados não interrompa a detecção de comandos, o Zoiudo utiliza os dois núcleos do ESP32:

Core 0: Gerenciamento da pilha TCP/IP, conexão segura (TLS) com o Telegram e o Web Server do WiFiManager.

Core 1: Captura de frames da câmera, processamento de JPEG e controle de interrupção do sensor PIR.

3. Validação de Credenciais
A segurança é baseada na verificação de chat_id. A função de tratamento de mensagens percorre o array de usuários autorizados antes de executar qualquer comando.

Se o ID não constar no array, o bot responde "Usuário não autorizado".

Simultaneamente, um log contendo Nome, ID e o texto da mensagem é disparado para a constante chat_id_master.

4. Gestão de Conectividade (Self-Healing)
O sistema possui uma rotina de recuperação automática de rede no loop() principal. Se o sinal de WiFi cair, o sistema tenta reconectar por 40 ciclos de 500ms. Caso falhe, o WifiMobile() é invocado, transformando o ESP32 em um Access Point para que o usuário reconfigure a rede via navegador no IP 192.168.4.1.