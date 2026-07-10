# Seguridad

## Modelo

CascadeDTL asume que los fixtures representan entradas ya autorizadas por un
sistema upstream. El binario valida la consistencia local del escenario antes de
procesar batches:

- participantes unicos;
- reservas con owner existente;
- paquetes con reserva y beneficiario validos;
- importes positivos;
- fees no negativos y no superiores al importe principal;
- limites de intentos, delays y legs dentro de rangos definidos.
- limites opcionales de lane para importe, fee y prioridad.

## Invariantes Esperadas

Durante una liquidacion normal:

- una reserva no debe tener importes negativos;
- un paquete liquidado debe tener un receipt consumido;
- un paquete diferido debe respetar `notBefore`;
- las prioridades deben ser deterministas para un mismo fixture;
- las cuentas de beneficiarios y fees deben reflejar el importe neto liquidado;
- el reporte final debe ser estable byte a byte para un fixture identico.

## Validacion Automatizada

La suite publica cubre:

- contrato de CLI;
- validacion de fixtures;
- accounting de reservas;
- consumo de receipts en flujos normales;
- reanudacion por ventana de retry;
- prioridad dinamica por congestion;
- rechazos por politicas de lane;
- reconciliacion agregada por lane;
- determinismo de reportes.

## Gestion De Dependencias

El nucleo C no depende de librerias externas. Node se usa solo para scripts y
tests. Dependabot esta configurado para npm y GitHub Actions.

## Alcance De Revision

El alcance principal es la logica de settlement en:

- `src/model.*`
- `src/ledger.*`
- `src/scheduler.*`
- `src/settlement.*`
- `tests/fixtures/*.json`

Los directorios `out/`, `build/` y `node_modules/` son artefactos locales.

## Reportes

Un reporte interno debe incluir:

- fixture o secuencia de batches reproducible;
- estado final JSON;
- diferencia esperada frente al resultado observado;
- impacto economico;
- propuesta de test de regresion.
