These files are JSON files compressed with Qt's internal qCompress()
function.  You can uncompress them by running the binary produced by 
this project with the "qz" argument as so:

 $ ../build/Json qz bench/*.qz

You can re-compress them similarly:

 $ ../build/Json qz bench/*.json

To view one of these files on stdout, say for "user.json.qz", you can do:

 $ ../build/Json qzcat bench/user.json.qz

