# LUMIO

Sistema Inteligente de Gestão de Ocupação e Iluminação utilizando Internet das Coisas (IoT).

## Objetivo

Monitorar a ocupação de ambientes em tempo real através de sensores infravermelhos conectados a um ESP32, atualizando automaticamente um dashboard web através do Firebase Realtime Database.

## Tecnologias Utilizadas

* ESP32
* Firebase Realtime Database
* HTML
* CSS
* JavaScript
* Sensores Infravermelhos
* Wi-Fi

## Funcionamento

O sistema utiliza duas barreiras infravermelhas para identificar a passagem de usuários.

Quando ocorre a sequência Sensor A → Sensor B, o sistema registra uma entrada.

Quando ocorre a sequência Sensor B → Sensor A, o sistema registra uma saída.

A quantidade de pessoas presentes é armazenada no Firebase e exibida em tempo real no dashboard web.

## Funcionalidades

* Contagem de pessoas
* Monitoramento remoto
* Dashboard em tempo real
* Controle automático de iluminação
* Integração com Firebase

## Aplicações

* Salas de aula
* Escritórios
* Laboratórios
* Ambientes corporativos
* Automação residencial

## Desenvolvido para

Projeto acadêmico UNIFAN – Engenharia de Software.
