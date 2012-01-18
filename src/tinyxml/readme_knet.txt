This folder contains a copy of the TinyXML library version 2.6.2, downloaded from http://www.grinninglizard.com/tinyxml/, and stripped down to only essential files needed to build kNet.

kNet does not require TinyXML to work, but the provided tool MessageCompiler does, and therefore TinyXML is embedded here for convenience. If you do not use MessageCompiler, you can set USE_TINYXML to FALSE in CMakeLists.txt, remove the MessageCompiler sample from build, and delete this folder to strip TinyXML from build.

For copyright and other information regarding TinyXML, please visit the site above.