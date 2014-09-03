/** @file
 * 
 * @ingroup implementationMaxExternalsGraph
 *
 * @brief j.pack# - External object for Max/MSP to bring Max values into a Jamoma Graph.
 *
 * @details
 *
 * @authors Tim Place, Trond Lossius
 *
 * @copyright Copyright © 2010 by Timothy Place @n
 * This code is licensed under the terms of the "New BSD License" @n
 * http://creativecommons.org/licenses/BSD/
 */


#include "maxGraph.h"
#include "TTGraphInput.h"


// Data Structure for this object
struct Pack {
   	t_object				    obj;
	TTGraphObjectBasePtr	graphObject;		// this _must_ be second
	TTPtr				    graphOutlets[16];	// this _must_ be third (for the setup call)
	TTDictionaryPtr		    graphDictionary;
	t_object*			    patcher;
    t_object*			    patcherview;
	TTPtr				    qelem;				// for clumping dirty events together
};
typedef Pack* PackPtr;


// Prototypes for methods
PackPtr	PackNew			(t_symbol* msg, long argc, t_atom* argv);
void   	PackFree		(PackPtr self);
void	PackStartTracking(PackPtr self);
t_max_err	PackNotify		(PackPtr self, t_symbol* s, t_symbol* msg, t_object* sender, TTPtr data);
void	PackQFn			(PackPtr self);
void   	PackAssist		(PackPtr self, void* b, long msg, long arg, char* dst);
void	PackInt			(PackPtr self, long value);
void	PackFloat		(PackPtr self, double value);
void	PackList		(PackPtr self, t_symbol* s, long ac, t_atom* ap);
void	PackAnything	(PackPtr self, t_symbol* s, long ac, t_atom* ap);
void	PackMessage		(PackPtr self, t_symbol* s, long ac, t_atom* ap);
void	PackAttribute	(PackPtr self, t_symbol* s, long ac, t_atom* ap);
TTErr  	PackReset		(PackPtr self, long vectorSize);
TTErr  	PackSetup		(PackPtr self);


// Globals
static t_class* sPackClass;


/************************************************************************************/
// Main() Function

int C74_EXPORT main(void)
{
	t_class* c;
	
	TTGraphInit();	
	common_symbols_init();
	
	c = class_new("j.pack-", (method)PackNew, (method)PackFree, sizeof(Pack), (method)0L, A_GIMME, 0);
	
	class_addmethod(c, (method)PackInt,				"int",				A_LONG, 0);
	class_addmethod(c, (method)PackFloat,			"float",			A_LONG, 0);
	class_addmethod(c, (method)PackList,			"list",				A_GIMME, 0);
	class_addmethod(c, (method)PackAnything,		"anything",			A_GIMME, 0);
	class_addmethod(c, (method)PackMessage,			"message",			A_GIMME, 0);
	class_addmethod(c, (method)PackAttribute,		"attribute",		A_GIMME, 0);
	
	class_addmethod(c, (method)MaxGraphReset,		"graph.reset",		A_CANT, 0);
	class_addmethod(c, (method)MaxGraphSetup,		"graph.setup",		A_CANT, 0);
	class_addmethod(c, (method)MaxGraphDrop,		"graph.drop",		A_CANT, 0);
	class_addmethod(c, (method)MaxGraphObject,		"graph.object",		A_CANT, 0);

 	class_addmethod(c, (method)PackNotify,			"notify",			A_CANT, 0); 
 	class_addmethod(c, (method)PackAssist,			"assist",			A_CANT, 0); 
    class_addmethod(c, (method)object_obex_dumpout,	"dumpout",			A_CANT, 0);  
		
	class_register(_sym_box, c);
	sPackClass = c;
	return 0;
}


/************************************************************************************/
// Object Creation Method

PackPtr PackNew(t_symbol* msg, long argc, t_atom* argv)
{
    PackPtr	self;
	TTValue	v;
	TTErr	err;
	
    self = PackPtr(object_alloc(sPackClass));
    if (self) {
    	object_obex_store((void*)self, _sym_dumpout, (t_object*)outlet_new(self, NULL));
		self->graphOutlets[0] = outlet_new(self, "graph.connect");
		
		v.resize(2);
		v[0] = "graph.input";
		v[1] = 1;
		err = TTObjectBaseInstantiate(TT("graph.object"), (TTObjectBasePtr*)&self->graphObject, v);
		((TTGraphInput*)self->graphObject->mKernel.instance())->setOwner(self->graphObject);

		if (!self->graphObject->mKernel.valid()) {
			object_error(SELF, "cannot load Jamoma object");
			return NULL;
		}
		
		self->graphDictionary = new TTDictionary;
		self->graphDictionary->setSchema(TT("none"));
		self->graphDictionary->append(TT("outlet"), 0);
		
		attr_args_process(self, argc, argv);
		
		self->qelem = qelem_new(self, (method)PackQFn);

		// PackStartTracking(self);
		defer_low(self, (method)PackStartTracking, NULL, 0, NULL);
	}
	return self;
}


// Memory Deallocation
void PackFree(PackPtr self)
{
	TTObjectBaseRelease((TTObjectBasePtr*)&self->graphObject);
	qelem_free(self->qelem);
}


/************************************************************************************/

t_max_err PackNotify(PackPtr self, t_symbol* s, t_symbol* msg, t_object* sender, TTPtr data)
{
	if (sender == self->patcherview) {
		if (msg == _sym_attr_modified) {
			t_symbol* name = (t_symbol*)object_method((t_object*)data, _sym_getname);
			if (name == _sym_dirty) {
				qelem_set(self->qelem);
			}
		}
		else if (msg == _sym_free)
			self->patcherview = NULL;
	}
	else {
		if (msg == _sym_free) {
			t_object*	sourceBox;  
			t_object*	sourceObject;
			long		sourceOutlet;
			t_object*	destBox;     
			t_object*	destObject;  
			long		destInlet;		

			#ifdef DEBUG_NOTIFICATIONS
			object_post(SELF, "patch line deleted");
			#endif // DEBUG_NOTIFICATIONS
			
			// get boxes and inlets
			sourceBox = jpatchline_get_box1(sender);
			if (!sourceBox)
				goto out;
			
			sourceObject = jbox_get_object(sourceBox);
			sourceOutlet = jpatchline_get_outletnum(sender);
			destBox = jpatchline_get_box2(sender);
			if (!destBox)
				goto out;
			destObject = jbox_get_object(destBox);
			destInlet = jpatchline_get_inletnum(sender);
			
			// if both boxes are graph objects 
			if ( zgetfn(sourceObject, gensym("graph.object")) && zgetfn(destObject, gensym("graph.object")) ) {
				#ifdef DEBUG_NOTIFICATIONS
				object_post(SELF, "deleting graph patchline!");
				#endif // DEBUG_NOTIFICATIONS
				
				object_method(destObject, gensym("graph.drop"), destInlet, sourceObject, sourceOutlet);
			}
		out:
			;
		}
	}
	return MAX_ERR_NONE;
}


void PackIterateResetCallback(PackPtr self, t_object* obj)
{
	t_max_err err = MAX_ERR_NONE;
	method graphResetMethod = zgetfn(obj, gensym("graph.reset"));
	
	if (graphResetMethod)
		err = (t_max_err)graphResetMethod(obj);
}


void PackIterateSetupCallback(PackPtr self, t_object* obj)
{
	t_max_err err = MAX_ERR_NONE;
	method graphSetupMethod = zgetfn(obj, gensym("graph.setup"));
	
	if (graphSetupMethod)
		err = (t_max_err)graphSetupMethod(obj);
}


void PackAttachToPatchlinesForPatcher(PackPtr self, t_object* patcher)
{
	t_object*	patchline = object_attr_getobj(patcher, _sym_firstline);
	t_object*	box = jpatcher_get_firstobject(patcher);
	
	while (patchline) {
		object_attach_byptr_register(self, patchline, _sym_nobox);
		patchline = object_attr_getobj(patchline, _sym_nextline);
	}
	
	while (box) {
		t_symbol*	classname = jbox_get_maxclass(box);
		
		if (classname == _sym_jpatcher) {
			t_object*	subpatcher = jbox_get_object(box);
			
			PackAttachToPatchlinesForPatcher(self, subpatcher);
		}
		box = jbox_get_nextobject(box);
	}
}


// Qelem function, which clumps together dirty notifications before making the new connections
void PackQFn(PackPtr self)
{
	t_atom result;
	
	#ifdef DEBUG_NOTIFICATIONS
	object_post(SELF, "patcher dirtied");
	#endif // DEBUG_NOTIFICATIONS
	
	object_method(self->patcher, gensym("iterate"), (method)PackIterateSetupCallback, self, PI_DEEP, &result);
	
	// attach to all of the patch cords so we will know if one is deleted
	// we are not trying to detach first -- hopefully this is okay and multiple attachments will be filtered (?)
	PackAttachToPatchlinesForPatcher(self, self->patcher);
}


// Start keeping track of edits and connections in the patcher
void PackStartTracking(PackPtr self)
{
	t_object*	patcher = NULL;
	t_object*	parent = NULL;
	t_object*	patcherview = NULL;
	t_max_err		err;
	t_atom		result;
	
	// first find the top-level patcher
	err = object_obex_lookup(self, gensym("#P"), &patcher);
	parent = patcher;
	while (parent) {
		patcher = parent;
		parent = object_attr_getobj(patcher, _sym_parentpatcher);
	}
	
	// now iterate recursively from the top-level patcher down through all of the subpatchers
	object_method(patcher, gensym("iterate"), (method)PackIterateResetCallback, self, PI_DEEP, &result);
	object_method(patcher, gensym("iterate"), (method)PackIterateSetupCallback, self, PI_DEEP, &result);
	
	
	// now let's attach to the patcherview to get notifications about any further changes to the patch cords
	// the patcher 'dirty' attribute is not modified for each change, but the patcherview 'dirty' attribute is
	if (!self->patcherview) {
		patcherview = jpatcher_get_firstview(patcher);
		self->patcherview = patcherview;
		self->patcher = patcher;
		object_attach_byptr_register(self, patcherview, _sym_nobox);			
	}

	// now we want to go a step further and attach to all of the patch cords 
	// this is how we will know if one is deleted
	PackAttachToPatchlinesForPatcher(self, self->patcher);
}


/************************************************************************************/
// Methods bound to input/inlets

// Method for Assistance Messages
void PackAssist(PackPtr self, void* b, long msg, long arg, char* dst)
{
	if (msg==1)			// Inlets
		strcpy (dst, "multichannel input and control messages");		
	else if (msg==2) {	// Outlets
		if (arg == 0)
			strcpy(dst, "multichannel output");
		else
			strcpy(dst, "dumpout");
	}
}


void PackInt(PackPtr self, long value)
{
	TTValue v = int(value);

	self->graphDictionary->setSchema(TT("number"));
	self->graphDictionary->setValue(v);
	((TTGraphInput*)self->graphObject->mKernel.instance())->push(*self->graphDictionary);
}


void PackFloat(PackPtr self, double value)
{
	TTValue v = value;
	
	self->graphDictionary->setSchema(TT("number"));
	self->graphDictionary->setValue(v);
	((TTGraphInput*)self->graphObject->mKernel.instance())->push(*self->graphDictionary);
}


void PackList(PackPtr self, t_symbol* s, long ac, t_atom* ap)
{
	TTValue v;
	
	v.resize(ac);
	for (int i=0; i<ac; i++) {
		switch (atom_gettype(ap+i)) {
			case A_LONG:
				v[i] = (int)atom_getlong(ap+i);
				break;
			case A_FLOAT:
				v[i] = atom_getfloat(ap+i);
				break;
			case A_SYM:
				v[i] = TT(atom_getsym(ap+i)->s_name);
				break;
			default:
				break;
		}
	}
	self->graphDictionary->setSchema(TT("array"));
	self->graphDictionary->setValue(v);
	((TTGraphInput*)self->graphObject->mKernel.instance())->push(*self->graphDictionary);
}


void PackAnything(PackPtr self, t_symbol* s, long ac, t_atom* ap)
{
	TTValue v;
	
	v.resize(ac+1);
	if (ac > 0) {
		self->graphDictionary->setSchema(TT("array"));
		v[0] = TT(s->s_name);
		for (int i=0; i<ac; i++) {
			switch (atom_gettype(ap+i)) {
				case A_LONG:
					v[i+1] = (int)atom_getlong(ap+i);
					break;
				case A_FLOAT:
					v[i+1] = atom_getfloat(ap+i);
					break;
				case A_SYM:
					v[i+1] = TT(atom_getsym(ap+i)->s_name);
					break;
				default:
					break;
			}
		}
	}
	else {
		self->graphDictionary->setSchema(TT("symbol"));
		v[0] = TT(s->s_name);
	}
	
	self->graphDictionary->setValue(v);
	((TTGraphInput*)self->graphObject->mKernel.instance())->push(*self->graphDictionary);
}


void PackMessage(PackPtr self, t_symbol* s, long ac, t_atom* ap)
{
	TTValue v;
	
	if (ac < 1)
		return;
	
	self->graphDictionary->setSchema(TT("message"));
	self->graphDictionary->append(TT("name"), TT(atom_getsym(ap)->s_name));
	
	v.resize(ac-1);
	if (ac > 2) {
		
		for (int i=0; i < ac-1; i++) {
			switch (atom_gettype(ap+i)) {
				case A_LONG:
					v[i+1] = (int)atom_getlong(ap+i);
					break;
				case A_FLOAT:
					v[i+1] = atom_getfloat(ap+i);
					break;
				case A_SYM:
					v[i+1] = TT(atom_getsym(ap+i)->s_name);
					break;
				default:
					break;
			}
		}
	}
	else if (ac > 1) {
		if (atom_gettype(ap+1) == A_SYM)
			v[0] = TT(s->s_name);
		else if (atom_gettype(ap+1) == A_LONG || atom_gettype(ap+1) == A_FLOAT)
			v[0] = atom_getfloat(ap+1);
	}
	
	self->graphDictionary->setValue(v);
	((TTGraphInput*)self->graphObject->mKernel.instance())->push(*self->graphDictionary);
}


void PackAttribute(PackPtr self, t_symbol* s, long ac, t_atom* ap)
{
	TTValue v;
	
	if (ac < 1)
		return;
	
	self->graphDictionary->setSchema(TT("attribute"));
	self->graphDictionary->append(TT("name"), TT(atom_getsym(ap)->s_name));
	
	v.resize(ac-1);
	if (ac > 2) {
		
		for (int i=0; i < ac-1; i++) {
			switch (atom_gettype(ap+i)) {
				case A_LONG:
					v[i+1] = (int)atom_getlong(ap+i);
					break;
				case A_FLOAT:
					v[i+1] = atom_getfloat(ap+i);
					break;
				case A_SYM:
					v[i+1] = TT(atom_getsym(ap+i)->s_name);
					break;
				default:
					break;
			}
		}
	}
	else if (ac > 1) {
		if (atom_gettype(ap+1) == A_SYM)
			v[0] = TT(s->s_name);
		else if (atom_gettype(ap+1) == A_LONG || atom_gettype(ap+1) == A_FLOAT)
			v[0] = atom_getfloat(ap+1);
	}
	
	self->graphDictionary->setValue(v);
	((TTGraphInput*)self->graphObject->mKernel.instance())->push(*self->graphDictionary);
}

