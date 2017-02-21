(c++-project
 '(
   "src"
   "elle/das/src"
   "elle/elle/src"
   "elle/reactor/src"
   "elle/cryptography/src"
   )
 "_build/linux64"
 (concat "./drake -j " (int-to-string system-cores-logical) " //build")
 ""
 '()
)
