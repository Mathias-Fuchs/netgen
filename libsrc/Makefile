debug:
	# https://stackoverflow.com/questions/1617286/easy-check-for-unresolved-symbols-in-shared-libraries
	g++ --shared core/*.cpp general/*.cpp csg/*.cpp geom2d/*.cpp gprim/*.cpp linalg/*.cpp meshing/*.cpp stlgeom/*.cpp --std=c++17 -Iinclude -I. -I/usr/include/tcl -Imeshing -fPIC -Wl,-z,defs
	# strategie: die Dateien weglassen, in denen Symbole vorkommen, die noch undefiniert sind
