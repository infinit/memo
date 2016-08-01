(c++-project
 '(
   "src"
   "elle/elle/src"
   "elle/reactor/src"
   "elle/cryptography/sources"
   )
 "_build/linux64"
 (concat "./drake -j " (int-to-string system-cores-logical) " //build")
 ""
 '()
)
