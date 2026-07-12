<div align="center">

<pre>
  ███████╗ ██████╗ ██╗   ██╗███╗   ██╗██████╗ 
  ██╔════╝██╔═══██╗██║   ██║████╗  ██║██╔══██╗
  ███████╗██║   ██║██║   ██║██╔██╗ ██║██║  ██║
  ╚════██║██║   ██║██║   ██║██║╚██╗██║██║  ██║
  ███████║╚██████╔╝╚██████╔╝██║ ╚████║██████╔╝
  ╚══════╝ ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝╚═════╝ 
  ██╗   ██╗██╗███████╗██╗ ██████╗ ███╗   ██╗
  ██║   ██║██║██╔════╝██║██╔═══██╗████╗  ██║
  ██║   ██║██║███████╗██║██║   ██║██╔██╗ ██║
  ╚██╗ ██╔╝██║╚════██║██║██║   ██║██║╚██╗██║
   ╚████╔╝ ██║███████║██║╚██████╔╝██║ ╚████║
    ╚═══╝  ╚═╝╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝
</pre>

<p><b>Dispositivo Vestível Assistivo de Baixo Custo · Sensoriamento Tridirecional · Habilitado para IoT</b></p>

<img src="https://img.shields.io/badge/Status-Ativo-brightgreen?logo=git" alt="Status" />
<img src="https://img.shields.io/badge/MCU-ESP32--C3-red?logo=espressif" alt="ESP32" />
<img src="https://img.shields.io/badge/Hardware-HC--SR04-blue" alt="Sensores" />
<img src="https://img.shields.io/badge/licen%C3%A7a-MIT-purple" alt="License" />

</div>

---

## O que é o SoundVision?

O **SoundVision** é uma tecnologia assistiva vestível, de código aberto e baixo custo, projetada para auxiliar a mobilidade de pessoas com deficiência visual. Ele também serve como uma robusta plataforma pedagógica para Aprendizagem Baseada em Projetos (PBL) no ensino de engenharia.

Chega de depender de soluções com um único sensor que deixam enormes zonas cegas. O SoundVision utiliza um arranjo tridirecional de sensores ultrassônicos acoplados a uma armação de óculos para fornecer percepção espacial completa, tanto frontal quanto lateral.

```text
Obstáculo → SoundVision (Sensores + ESP32) → Feedback Auditivo Espacial
```

Projetado para ser acessível (custo de componentes inferior a US$ 30 / R$ 150), replicável e altamente eficaz tanto para os usuários finais quanto para os estudantes de engenharia.

## Como Funciona

O SoundVision utiliza uma Máquina de Estados Finitos (FSM) não bloqueante para garantir processamento em tempo real com uma latência determinística de ~56 ms:

<div align="center">
  <img src="docs/fsm_diagram.png" alt="Arquitetura da Máquina de Estados Finitos" width="600" />
  <p><i>Arquitetura do Sistema e Fluxo de Dados</i></p>
</div>

## Funcionalidades

*   **Arranjo Tridirecional** — Elimina zonas cegas laterais utilizando três zonas ultrassônicas independentes.
*   **Processamento em Tempo Real** — A arquitetura FSM customizada garante um tempo de ciclo de ~56ms (17,8 Hz) para feedback instantâneo.
*   **Feedback Auditivo Espacial** — Frequências tonais distintas para cada direção (Esquerda, Centro, Direita) permitem uma navegação intuitiva sem a necessidade de fones de ouvido.
*   **Dashboard de Telemetria IoT** — O Access Point Wi-Fi integrado hospeda um dashboard em tempo real via WebSockets para monitorar o estado dos sensores e configurar parâmetros.
*   **Custo Extremamente Baixo** — O custo total da Lista de Materiais (BOM) é inferior a US$ 30, sendo ideal para replicação em larga escala em ambientes acadêmicos.
*   **Vestível e Ergonômico** — Estrutura leve em PLA impressa em 3D, projetada para se acoplar a armações de óculos convencionais.

## Stack de Tecnologias

| Componente | Tecnologia |
|---|---|
| **Microcontrolador** | ESP32-C3 Mini (RISC-V 32-bit) |
| **Sensores** | 3x Transdutores Ultrassônicos HC-SR04 |
| **Firmware** | C++ (ESP-IDF / Arduino Core) |
| **Dashboard IoT** | HTML, Vanilla JS, WebSockets |
| **Alimentação** | LiPo 1000mAh + Módulo de Carga TP4056 |
| **Estrutura** | PLA Impresso em 3D |

## Montagem e Implantação

O **SoundVision** é otimizado para implantação e experimentação acadêmica rápida.

```bash
# Para clonar o firmware e fazer o upload via PlatformIO ou Arduino IDE:
git clone https://github.com/theHerick/soundvision.git
cd soundvision/firmware
```

**Requisitos de Hardware:**
- 1x ESP32-C3 Mini
- 3x HC-SR04
- 3x Buzzers Piezoelétricos
- 1x Módulo TP4056 + Bateria LiPo 3.7V
- Impressora 3D padrão para os arquivos de armação `.stl`.

## Estrutura do Projeto

```text
soundvision/
├── firmware/           # Código fonte em C++ para ESP32-C3 (FSM + WebSockets)
├── hardware/
│   ├── schematics/     # Diagramas de circuito e esquemas de ligação
│   └── 3d_models/      # Arquivos .STL para a armação dos óculos em 3D
├── docs/               # Guias pedagógicos e planos de aula (PBL)
└── README.md           # Visão geral do projeto
```

<div align="center">
Feito com 💡 para a Educação em Engenharia e Impacto Social.
<br>
Tiburski, H. B. | Alves, M. D. C. | Sausen, J. P. | de Campos, M.
</div>
