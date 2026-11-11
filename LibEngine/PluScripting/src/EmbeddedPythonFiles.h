//
// Created by Plutex on 11/11/26.
//

#ifndef PLUENGINE_EMBEDDEDPYTHONFILES_H
#define PLUENGINE_EMBEDDEDPYTHONFILES_H

// Private to PluScripting: the table cmake/PluEmbedPython.cmake generates from python/*.py. Kept
// free of engine includes because the generated source skips the precompiled header.

namespace Plu::Detail
{
	struct EmbeddedPythonFile
	{
		const char* Name;              // file name without ".py"
		const unsigned char* Source;   // UTF-8, null-terminated
	};

	// Terminated by an entry whose Name is null.
	extern const EmbeddedPythonFile gEmbeddedPythonFiles[];
}

#endif //PLUENGINE_EMBEDDEDPYTHONFILES_H
