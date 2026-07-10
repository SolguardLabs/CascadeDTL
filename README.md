# CascadeDTL

![banner](./assets/banner.png)

CascadeDTL es un simulador de liquidacion DTL por batches. Modela paquetes de
settlement cargados desde fixtures JSON, colas diferidas, ventanas de retry y
prioridad dinamica por congestion.

El binario esta escrito en C11 y no requiere servicios externos. Los tests de
contrato se ejecutan con `node:test` contra estados finales emitidos por la CLI.

## Componentes

- `src/json.*`: parser JSON autocontenido para fixtures de auditoria.
- `src/model.*`: entidades de dominio, validacion y carga de escenarios.
- `src/risk.*`: limites operativos opcionales por lane.
- `src/ledger.*`: reservas, receipts y accounting de participantes.
- `src/scheduler.*`: seleccion de paquetes por prioridad y congestion.
- `src/settlement.*`: ejecucion de batches y retries.
- `src/reconcile.*`: agregados de reservas y paquetes por lane.
- `src/report.*`: serializacion JSON estable para tests.

## Requisitos

- Node.js 20 o superior.
- Un compilador C disponible como `cc`, `gcc`, `clang` o `cl`.

En Windows, si se usa MSVC, ejecutar los comandos desde una Developer Prompt.

## Uso

Compilar:

```bash
node scripts/build.mjs
```

Validar un fixture:

```bash
out/cascadedtl validate tests/fixtures/balanced_settlement.json
```

Ejecutar una liquidacion:

```bash
out/cascadedtl run tests/fixtures/balanced_settlement.json --json --events
```

## Tests

```bash
npm test
```

El script compila el binario y ejecuta:

```bash
node --test "tests/node/*.test.js"
```

## Fixtures

Los escenarios JSON definen:

- participantes y cuentas de fees;
- politicas opcionales de lane (`maxPacketAmount`, `maxFeeBps`,
  `minPriority`, `requireFeeAccount`);
- reservas disponibles por lane;
- batches con epoch logico;
- paquetes de settlement con importe, fee, prioridad, peso de congestion,
  intentos maximos y numero de legs;
- planes de fallo para simular ramas de liquidacion no terminales.

Los importes son enteros. No se aceptan decimales ni notacion exponencial.

## Estado Del Lab

El proyecto esta disenado como un repositorio de auditoria autocontenido. La
salida JSON final es el contrato principal para herramientas externas y tests de
regresion.
