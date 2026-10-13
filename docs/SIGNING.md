# Assinatura de código — gratuita e segura

Sem assinatura, o SmartScreen marca o instalador como "origem desconhecida".
Opções gratuitas reais (verificadas out/2026):

## 1. SignPath Foundation (recomendado para OSS)
- **Custo**: zero para projetos open-source qualificáveis (repo público).
- **Nível**: certificado OV — tira o aviso amarelo após reputação.
- **Segurança**: a chave privada **nunca sai da nuvem deles**; o CI envia o hash,
  eles devolvem assinado. Sem risco de vazamento da sua chave.
- **Passos**: publique o repo → solicite em `signpath.org` (ou `signpath.io/solutions/open-source-community`)
  → receba `SIGNPATH_API_TOKEN` → salve em Settings > Secrets > Actions →
  o step `Assinar instalador` do `release.yml` passa a assinar.
- Bloco real (trocar placeholders) quando tiver o token:
  ```yaml
  - uses: signpath/github-action-submit-signing-request@v1
    with:
      api-token: ${{ secrets.SIGNPATH_API_TOKEN }}
      organization-id: '<sua-org>'
      project-slug: 'nanotask'
      signing-policy-slug: 'release-signing'
      artifact-configuration-slug: 'installer'
      github-artifact-id: '<id do artefato>'
  ```

## 2. Certum Open Source (alternativa)
- CA polonesa com certificado Authenticode **gratuito para OSS**. Exige validação
  de identidade + token físico/cloud. Processo mais burocrático, mas a chave é sua.
- Use se quiser assinar localmente (`signtool sign /fd SHA256 /tr http://time.certum.pl ...`).

## 3. Desenvolvimento local (nunca distribua)
- Certificado autoassinado só para testar o pipeline:
  ```powershell
  $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=NanoTask Dev'
  signtool sign /fd SHA256 /a /f dev.pfx NanoTask.exe
  ```
  O Windows vai continuar alertando — é esperado, serve só para validar o fluxo.

## Regras de segurança
1. **Nunca commite `.pfx`/tokens** — só via GitHub Secrets.
2. Assine **EXE + instalador** (o Inno propaga; SmartScreen avalia ambos).
3. Timestamp sempre (`/tr http://timestamp.digicert.com`) — assinatura continua
   válida após expirar o certificado.
4. O auto-update (`UpdateManager`) verifica SHA-256 independente da assinatura:
   defesa em duas camadas.
