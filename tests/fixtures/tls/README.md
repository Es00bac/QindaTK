<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# TLS test fixtures

`tst_provider_tls` runs the in-process SMTP, IMAP and CalDAV test servers
over TLS with these files. They are test-only: the private key protects
nothing and must never be trusted outside the tests.

| File | Content |
| --- | --- |
| `ca.pem` | Self-signed test CA ("QindaTK Test CA"). Its key was discarded. |
| `server.pem` | Server certificate signed by the test CA, SAN `IP:127.0.0.1`, `DNS:localhost`, valid until 2126 |
| `server.key` | Unencrypted RSA 2048 key for `server.pem` |

The tests add `ca.pem` to the default TLS configuration only for the cases
that expect a trusted server; the untrusted case uses the system roots and
must fail the handshake before any credential is sent.

To regenerate (a new CA replaces both files):

```
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem -days 36500 \
  -subj "/CN=QindaTK Test CA" -addext "basicConstraints=critical,CA:TRUE" \
  -addext "keyUsage=critical,keyCertSign,cRLSign"
openssl req -newkey rsa:2048 -nodes -keyout server.key -out server.csr -subj "/CN=localhost"
printf 'subjectAltName=IP:127.0.0.1,DNS:localhost\nextendedKeyUsage=serverAuth\n' > ext.cnf
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
  -out server.pem -days 36500 -extfile ext.cnf
rm ca.key ca.srl server.csr ext.cnf
```
