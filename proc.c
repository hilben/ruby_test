/**********************************************************************

  proc.c - Proc, Bindng, Env

  $Author$
  $Date$
  created at: Wed Jan 17 12:13:14 2007

  Copyright (C) 2004-2007 Koichi Sasada

**********************************************************************/

#include "eval_intern.h"
#include "gc.h"

struct METHOD {
    VALUE klass, rklass;
    VALUE recv;
    ID id, oid;
    NODE *body;
};

VALUE rb_cUnboundMethod;
VALUE rb_cMethod;
VALUE rb_cBinding;
VALUE rb_cProc;
VALUE rb_cEnv;

static VALUE bmcall(VALUE, VALUE);
static int method_arity(VALUE);
static VALUE rb_obj_is_method(VALUE m);

/* Env */

static void
env_free(void *ptr)
{
    yarv_env_t *env;
    FREE_REPORT_ENTER("env");
    if (ptr) {
	env = ptr;
	FREE_UNLESS_NULL(env->env);
	ruby_xfree(ptr);
    }
    FREE_REPORT_LEAVE("env");
}

static void
env_mark(void *ptr)
{
    yarv_env_t *env;
    MARK_REPORT_ENTER("env");
    if (ptr) {
	env = ptr;
	if (env->env) {
	    /* TODO: should mark more restricted range */
	    GC_INFO("env->env\n");
	    rb_gc_mark_locations(env->env, env->env + env->env_size);
	}
	GC_INFO("env->prev_envval\n");
	MARK_UNLESS_NULL(env->prev_envval);

	if (env->block.iseq) {
	    if (BUILTIN_TYPE(env->block.iseq) == T_NODE) {
		MARK_UNLESS_NULL((VALUE)env->block.iseq);
	    }
	    else {
		MARK_UNLESS_NULL(env->block.iseq->self);
	    }
	}
    }
    MARK_REPORT_LEAVE("env");
}

VALUE
yarv_env_alloc(void)
{
    VALUE obj;
    yarv_env_t *env;
    obj = Data_Make_Struct(rb_cEnv, yarv_env_t, env_mark, env_free, env);
    env->env = 0;
    env->prev_envval = 0;
    env->block.iseq = 0;
    return obj;
}

/* Proc */

static void
proc_free(void *ptr)
{
    FREE_REPORT_ENTER("proc");
    if (ptr) {
	ruby_xfree(ptr);
    }
    FREE_REPORT_LEAVE("proc");
}

static void
proc_mark(void *ptr)
{
    yarv_proc_t *proc;
    MARK_REPORT_ENTER("proc");
    if (ptr) {
	proc = ptr;
	MARK_UNLESS_NULL(proc->envval);
	MARK_UNLESS_NULL(proc->blockprocval);
	MARK_UNLESS_NULL((VALUE)proc->special_cref_stack);
	if (proc->block.iseq && YARV_IFUNC_P(proc->block.iseq)) {
	    MARK_UNLESS_NULL((VALUE)(proc->block.iseq));
	}
    }
    MARK_REPORT_LEAVE("proc");
}

static VALUE
proc_alloc(VALUE klass)
{
    VALUE obj;
    yarv_proc_t *proc;
    obj = Data_Make_Struct(klass, yarv_proc_t, proc_mark, proc_free, proc);
    MEMZERO(proc, yarv_proc_t, 1);
    return obj;
}

VALUE
yarv_proc_alloc(void)
{
    return proc_alloc(rb_cProc);
}

VALUE
yarv_obj_is_proc(VALUE proc)
{
    if (TYPE(proc) == T_DATA &&
	RDATA(proc)->dfree == (RUBY_DATA_FUNC) proc_free) {
	return Qtrue;
    }
    else {
	return Qfalse;
    }
}

static VALUE
proc_dup(VALUE self)
{
    VALUE procval = proc_alloc(rb_cProc);
    yarv_proc_t *src, *dst;
    GetProcPtr(self, src);
    GetProcPtr(procval, dst);

    dst->block = src->block;
    dst->envval = src->envval;
    dst->safe_level = dst->safe_level;
    dst->special_cref_stack = src->special_cref_stack;
    
    return procval;
}

static VALUE
yarv_proc_dup(VALUE self)
{
    return proc_dup(self);
}

static VALUE
proc_clone(VALUE self)
{
    VALUE procval = proc_dup(self);
    CLONESETUP(procval, self);
    return procval;
}

/* Binding */

static void
binding_free(void *ptr)
{
    yarv_binding_t *bind;
    FREE_REPORT_ENTER("binding");
    if (ptr) {
	bind = ptr;
	ruby_xfree(ptr);
    }
    FREE_REPORT_LEAVE("binding");
}

static void
binding_mark(void *ptr)
{
    yarv_binding_t *bind;
    MARK_REPORT_ENTER("binding");
    if (ptr) {
	bind = ptr;
	MARK_UNLESS_NULL(bind->env);
	MARK_UNLESS_NULL((VALUE)bind->cref_stack);
    }
    MARK_REPORT_LEAVE("binding");
}

static VALUE
binding_alloc(VALUE klass)
{
    VALUE obj;
    yarv_binding_t *bind;
    obj = Data_Make_Struct(klass, yarv_binding_t,
			   binding_mark, binding_free, bind);
    MEMZERO(bind, yarv_binding_t, 1);
    return obj;
}

static VALUE
binding_dup(VALUE self)
{
    VALUE bindval = binding_alloc(rb_cBinding);
    yarv_binding_t *src, *dst;
    GetBindingPtr(self, src);
    GetBindingPtr(bindval, dst);
    dst->env = src->env;
    dst->cref_stack = src->cref_stack;
    return bindval;
}

static VALUE
binding_clone(VALUE self)
{
    VALUE bindval = binding_dup(self);
    CLONESETUP(bindval, self);
    return bindval;
}

VALUE
rb_binding_new(void)
{
    yarv_thread_t *th = GET_THREAD();
    yarv_control_frame_t *cfp = th_get_ruby_level_cfp(th, th->cfp);
    VALUE bindval = binding_alloc(rb_cBinding);
    yarv_binding_t *bind;

    GetBindingPtr(bindval, bind);
    bind->env = th_make_env_object(th, cfp);
    bind->cref_stack = ruby_cref();
    return bindval;
}

/*
 *  call-seq:
 *     binding -> a_binding
 *  
 *  Returns a +Binding+ object, describing the variable and
 *  method bindings at the point of call. This object can be used when
 *  calling +eval+ to execute the evaluated command in this
 *  environment. Also see the description of class +Binding+.
 *     
 *     def getBinding(param)
 *       return binding
 *     end
 *     b = getBinding("hello")
 *     eval("param", b)   #=> "hello"
 */

static VALUE
rb_f_binding(VALUE self)
{
    return rb_binding_new();
}

/*
 *  call-seq:
 *     binding.eval(string [, filename [,lineno]])  => obj
 *
 *  Evaluates the Ruby expression(s) in <em>string</em>, in the
 *  <em>binding</em>'s context.  If the optional <em>filename</em> and
 *  <em>lineno</em> parameters are present, they will be used when
 *  reporting syntax errors.
 *
 *     def getBinding(param)
 *       return binding
 *     end
 *     b = getBinding("hello")
 *     b.eval("param")   #=> "hello"
 */

static VALUE
bind_eval(int argc, VALUE *argv, VALUE bindval)
{
    VALUE args[4];

    rb_scan_args(argc, argv, "12", &args[0], &args[2], &args[3]);
    args[1] = bindval;
    return rb_f_eval(argc+1, args, Qnil /* self will be searched in eval */);
}

#define PROC_TSHIFT (FL_USHIFT+1)
#define PROC_TMASK  (FL_USER1|FL_USER2|FL_USER3)
#define PROC_TMAX   (PROC_TMASK >> PROC_TSHIFT)
#define PROC_NOSAFE FL_USER4

#define SAFE_LEVEL_MAX PROC_TMASK

static VALUE
proc_new(VALUE klass, int is_lambda)
{
    VALUE procval = Qnil;
    yarv_thread_t *th = GET_THREAD();
    yarv_control_frame_t *cfp = th->cfp;
    yarv_block_t *block;

    if ((GC_GUARDED_PTR_REF(cfp->lfp[0])) != 0 &&
	!YARV_CLASS_SPECIAL_P(cfp->lfp[0])) {
	block = GC_GUARDED_PTR_REF(cfp->lfp[0]);
    }
    else {
	cfp = YARV_PREVIOUS_CONTROL_FRAME(cfp);
	if ((GC_GUARDED_PTR_REF(cfp->lfp[0])) != 0 &&
	    !YARV_CLASS_SPECIAL_P(cfp->lfp[0])) {
	    block = GC_GUARDED_PTR_REF(cfp->lfp[0]);

	    if (is_lambda) {
		rb_warn("tried to create Proc object without a block");
	    }
	}
	else {
	    rb_raise(rb_eArgError,
		     "tried to create Proc object without a block");
	}
    }

    cfp = YARV_PREVIOUS_CONTROL_FRAME(cfp);
    procval = th_make_proc(th, cfp, block);

    if (is_lambda) {
	yarv_proc_t *proc;
	GetProcPtr(procval, proc);
	proc->is_lambda = Qtrue;
    }
    return procval;
}

/*
 *  call-seq:
 *     Proc.new {|...| block } => a_proc
 *     Proc.new                => a_proc
 *  
 *  Creates a new <code>Proc</code> object, bound to the current
 *  context. <code>Proc::new</code> may be called without a block only
 *  within a method with an attached block, in which case that block is
 *  converted to the <code>Proc</code> object.
 *     
 *     def proc_from
 *       Proc.new
 *     end
 *     proc = proc_from { "hello" }
 *     proc.call   #=> "hello"
 */

static VALUE
rb_proc_s_new(VALUE klass)
{
    return proc_new(klass, Qfalse);
}

/*
 * call-seq:
 *   proc   { |...| block }  => a_proc
 *
 * Equivalent to <code>Proc.new</code>.
 */

VALUE
rb_block_proc(void)
{
    return proc_new(rb_cProc, Qfalse);
}

VALUE
rb_block_lambda(void)
{
    return proc_new(rb_cProc, Qtrue);
}

VALUE
rb_f_lambda(void)
{
    rb_warn("rb_f_lambda() is deprecated; use rb_block_proc() instead");
    return rb_block_lambda();
}

/*
 * call-seq:
 *   lambda { |...| block }  => a_proc
 *
 * Equivalent to <code>Proc.new</code>, except the resulting Proc objects
 * check the number of parameters passed when called.
 */

static VALUE
proc_lambda(void)
{
    return rb_block_lambda();
}

VALUE
proc_invoke(VALUE self, VALUE args, VALUE alt_self, VALUE alt_klass)
{
    yarv_proc_t *proc;
    GetProcPtr(self, proc);

    /* ignore self and klass */
    return th_invoke_proc(GET_THREAD(), proc, proc->block.self,
			  RARRAY_LEN(args), RARRAY_PTR(args));
}

/* CHECKME: are the argument checking semantics correct? */

/*
 *  call-seq:
 *     prc.call(params,...)   => obj
 *     prc[params,...]        => obj
 *  
 *  Invokes the block, setting the block's parameters to the values in
 *  <i>params</i> using something close to method calling semantics.
 *  Generates a warning if multiple values are passed to a proc that
 *  expects just one (previously this silently converted the parameters
 *  to an array).
 *
 *  For procs created using <code>Kernel.proc</code>, generates an
 *  error if the wrong number of parameters
 *  are passed to a proc with multiple parameters. For procs created using
 *  <code>Proc.new</code>, extra parameters are silently discarded.
 *
 *  Returns the value of the last expression evaluated in the block. See
 *  also <code>Proc#yield</code>.
 *     
 *     a_proc = Proc.new {|a, *b| b.collect {|i| i*a }}
 *     a_proc.call(9, 1, 2, 3)   #=> [9, 18, 27]
 *     a_proc[9, 1, 2, 3]        #=> [9, 18, 27]
 *     a_proc = Proc.new {|a,b| a}
 *     a_proc.call(1,2,3)
 *     
 *  <em>produces:</em>
 *     
 *     prog.rb:5: wrong number of arguments (3 for 2) (ArgumentError)
 *     	from prog.rb:4:in `call'
 *     	from prog.rb:5
 */

static VALUE
proc_call(int argc, VALUE *argv, VALUE procval)
{
    yarv_proc_t *proc;
    GetProcPtr(procval, proc);
    return th_invoke_proc(GET_THREAD(), proc, proc->block.self, argc, argv);
}

static VALUE
proc_yield(int argc, VALUE *argv, VALUE procval)
{
    yarv_proc_t *proc;
    GetProcPtr(procval, proc);
    return th_invoke_proc(GET_THREAD(), proc, proc->block.self, argc, argv);
}

VALUE
rb_proc_call(VALUE proc, VALUE args)
{
    return proc_invoke(proc, args, Qundef, 0);
}

/*
 *  call-seq:
 *     prc.arity -> fixnum
 *  
 *  Returns the number of arguments that would not be ignored. If the block
 *  is declared to take no arguments, returns 0. If the block is known
 *  to take exactly n arguments, returns n. If the block has optional
 *  arguments, return -n-1, where n is the number of mandatory
 *  arguments. A <code>proc</code> with no argument declarations
 *  is the same a block declaring <code>||</code> as its arguments.
 *     
 *     Proc.new {}.arity          #=>  0
 *     Proc.new {||}.arity        #=>  0
 *     Proc.new {|a|}.arity       #=>  1
 *     Proc.new {|a,b|}.arity     #=>  2
 *     Proc.new {|a,b,c|}.arity   #=>  3
 *     Proc.new {|*a|}.arity      #=> -1
 *     Proc.new {|a,*b|}.arity    #=> -2
 */

static VALUE
proc_arity(VALUE self)
{
    yarv_proc_t *proc;
    yarv_iseq_t *iseq;
    GetProcPtr(self, proc);
    iseq = proc->block.iseq;
    if (iseq && BUILTIN_TYPE(iseq) != T_NODE) {
	if (iseq->arg_rest == 0 && iseq->arg_opts == 0) {
	    return INT2FIX(iseq->argc);
	}
	else {
	    return INT2FIX(-iseq->argc - 1);
	}
    }
    else {
	return INT2FIX(-1);
    }
}

int
rb_proc_arity(VALUE proc)
{
    return FIX2INT(proc_arity(proc));
}

/*
 * call-seq:
 *   prc == other_proc   =>  true or false
 *
 * Return <code>true</code> if <i>prc</i> is the same object as
 * <i>other_proc</i>, or if they are both procs with the same body.
 */

static VALUE
proc_eq(VALUE self, VALUE other)
{
    if (self == other) {
	return Qtrue;
    }
    else {
	if (TYPE(other)          == T_DATA &&
	    RBASIC(other)->klass == rb_cProc &&
	    CLASS_OF(self)       == CLASS_OF(other)) {
	    yarv_proc_t *p1, *p2;
	    GetProcPtr(self, p1);
	    GetProcPtr(other, p2);
	    if (p1->block.iseq == p2->block.iseq && p1->envval == p2->envval) {
		return Qtrue;
	    }
	}
    }
    return Qfalse;
}

/*
 * call-seq:
 *   prc.hash   =>  integer
 *
 * Return hash value corresponding to proc body.
 */

static VALUE
proc_hash(VALUE self)
{
    int hash;
    yarv_proc_t *proc;
    GetProcPtr(self, proc);
    hash = (long)proc->block.iseq;
    hash ^= (long)proc->envval;
    hash ^= (long)proc->block.lfp >> 16;
    return INT2FIX(hash);
}

/*
 * call-seq:
 *   prc.to_s   => string
 *
 * Shows the unique identifier for this proc, along with
 * an indication of where the proc was defined.
 */

static VALUE
proc_to_s(VALUE self)
{
    VALUE str = 0;
    yarv_proc_t *proc;
    char *cname = rb_obj_classname(self);
    yarv_iseq_t *iseq;
    
    GetProcPtr(self, proc);
    iseq = proc->block.iseq;

    if (YARV_NORMAL_ISEQ_P(iseq)) {
	int line_no = 0;
	
	if (iseq->insn_info_tbl) {
	    line_no = iseq->insn_info_tbl[0].line_no;
	}
	str = rb_sprintf("#<%s:%lx@%s:%d>", cname, self,
			 RSTRING_PTR(iseq->file_name),
			 line_no);
    }
    else {
	str = rb_sprintf("#<%s:%p>", cname, proc->block.iseq);
    }

    if (OBJ_TAINTED(self)) {
	OBJ_TAINT(str);
    }
    return str;
}

/*
 *  call-seq:
 *     prc.to_proc -> prc
 *  
 *  Part of the protocol for converting objects to <code>Proc</code>
 *  objects. Instances of class <code>Proc</code> simply return
 *  themselves.
 */

static VALUE
proc_to_proc(VALUE self)
{
    return self;
}

/*
 *  call-seq:
 *     prc.binding    => binding
 *  
 *  Returns the binding associated with <i>prc</i>. Note that
 *  <code>Kernel#eval</code> accepts either a <code>Proc</code> or a
 *  <code>Binding</code> object as its second parameter.
 *     
 *     def fred(param)
 *       proc {}
 *     end
 *     
 *     b = fred(99)
 *     eval("param", b.binding)   #=> 99
 *     eval("param", b)           #=> 99
 */

void
bm_mark(struct METHOD *data)
{
    rb_gc_mark(data->rklass);
    rb_gc_mark(data->klass);
    rb_gc_mark(data->recv);
    rb_gc_mark((VALUE)data->body);
}

NODE *rb_get_method_body(VALUE klass, ID id, ID *idp);

static VALUE
mnew(VALUE klass, VALUE obj, ID id, VALUE mklass)
{
    VALUE method;
    NODE *body;
    struct METHOD *data;
    VALUE rklass = klass;
    ID oid = id;

  again:
    if ((body = rb_get_method_body(klass, id, 0)) == 0) {
	print_undef(rklass, oid);
    }

    klass = body->nd_clss;
    body = body->nd_body;

    if (nd_type(body) == NODE_ZSUPER) {
	klass = RCLASS(klass)->super;
	goto again;
    }

    while (rklass != klass &&
	   (FL_TEST(rklass, FL_SINGLETON) || TYPE(rklass) == T_ICLASS)) {
	rklass = RCLASS(rklass)->super;
    }
    if (TYPE(klass) == T_ICLASS)
	klass = RBASIC(klass)->klass;
    method = Data_Make_Struct(mklass, struct METHOD, bm_mark, -1, data);
    data->klass = klass;
    data->recv = obj;

    data->id = id;
    data->body = body;
    data->rklass = rklass;
    data->oid = oid;
    OBJ_INFECT(method, klass);

    return method;
}


/**********************************************************************
 *
 * Document-class : Method
 *
 *  Method objects are created by <code>Object#method</code>, and are
 *  associated with a particular object (not just with a class). They
 *  may be used to invoke the method within the object, and as a block
 *  associated with an iterator. They may also be unbound from one
 *  object (creating an <code>UnboundMethod</code>) and bound to
 *  another.
 *     
 *     class Thing
 *       def square(n)
 *         n*n
 *       end
 *     end
 *     thing = Thing.new
 *     meth  = thing.method(:square)
 *     
 *     meth.call(9)                 #=> 81
 *     [ 1, 2, 3 ].collect(&meth)   #=> [1, 4, 9]
 *     
 */

/*
 * call-seq:
 *   meth == other_meth  => true or false
 *
 * Two method objects are equal if that are bound to the same
 * object and contain the same body.
 */


static VALUE
method_eq(VALUE method, VALUE other)
{
    struct METHOD *m1, *m2;

    if (TYPE(other) != T_DATA
	|| RDATA(other)->dmark != (RUBY_DATA_FUNC) bm_mark)
	return Qfalse;
    if (CLASS_OF(method) != CLASS_OF(other))
	return Qfalse;

    Data_Get_Struct(method, struct METHOD, m1);
    Data_Get_Struct(other, struct METHOD, m2);

    if (m1->klass != m2->klass || m1->rklass != m2->rklass ||
	m1->recv != m2->recv || m1->body != m2->body)
	return Qfalse;

    return Qtrue;
}

/*
 * call-seq:
 *    meth.hash   => integer
 *
 * Return a hash value corresponding to the method object.
 */

static VALUE
method_hash(VALUE method)
{
    struct METHOD *m;
    long hash;

    Data_Get_Struct(method, struct METHOD, m);
    hash = (long)m->klass;
    hash ^= (long)m->rklass;
    hash ^= (long)m->recv;
    hash ^= (long)m->body;

    return INT2FIX(hash);
}

/*
 *  call-seq:
 *     meth.unbind    => unbound_method
 *  
 *  Dissociates <i>meth</i> from it's current receiver. The resulting
 *  <code>UnboundMethod</code> can subsequently be bound to a new object
 *  of the same class (see <code>UnboundMethod</code>).
 */

static VALUE
method_unbind(VALUE obj)
{
    VALUE method;
    struct METHOD *orig, *data;

    Data_Get_Struct(obj, struct METHOD, orig);
    method =
	Data_Make_Struct(rb_cUnboundMethod, struct METHOD, bm_mark, free,
			 data);
    data->klass = orig->klass;
    data->recv = Qundef;
    data->id = orig->id;
    data->body = orig->body;
    data->rklass = orig->rklass;
    data->oid = orig->oid;
    OBJ_INFECT(method, obj);

    return method;
}

/*
 *  call-seq:
 *     meth.receiver    => object
 *  
 *  Returns the bound receiver of the method object.
 */

static VALUE
method_receiver(VALUE obj)
{
    struct METHOD *data;

    Data_Get_Struct(obj, struct METHOD, data);
    return data->recv;
}

/*
 *  call-seq:
 *     meth.name    => string
 *  
 *  Returns the name of the method.
 */

static VALUE
method_name(VALUE obj)
{
    struct METHOD *data;

    Data_Get_Struct(obj, struct METHOD, data);
    return rb_str_new2(rb_id2name(data->id));
}

/*
 *  call-seq:
 *     meth.owner    => class_or_module
 *  
 *  Returns the class or module that defines the method.
 */

static VALUE
method_owner(VALUE obj)
{
    struct METHOD *data;

    Data_Get_Struct(obj, struct METHOD, data);
    return data->klass;
}

/*
 *  call-seq:
 *     obj.method(sym)    => method
 *  
 *  Looks up the named method as a receiver in <i>obj</i>, returning a
 *  <code>Method</code> object (or raising <code>NameError</code>). The
 *  <code>Method</code> object acts as a closure in <i>obj</i>'s object
 *  instance, so instance variables and the value of <code>self</code>
 *  remain available.
 *     
 *     class Demo
 *       def initialize(n)
 *         @iv = n
 *       end
 *       def hello()
 *         "Hello, @iv = #{@iv}"
 *       end
 *     end
 *     
 *     k = Demo.new(99)
 *     m = k.method(:hello)
 *     m.call   #=> "Hello, @iv = 99"
 *     
 *     l = Demo.new('Fred')
 *     m = l.method("hello")
 *     m.call   #=> "Hello, @iv = Fred"
 */

VALUE
rb_obj_method(VALUE obj, VALUE vid)
{
    return mnew(CLASS_OF(obj), obj, rb_to_id(vid), rb_cMethod);
}

/*
 *  call-seq:
 *     mod.instance_method(symbol)   => unbound_method
 *  
 *  Returns an +UnboundMethod+ representing the given
 *  instance method in _mod_.
 *     
 *     class Interpreter
 *       def do_a() print "there, "; end
 *       def do_d() print "Hello ";  end
 *       def do_e() print "!\n";     end
 *       def do_v() print "Dave";    end
 *       Dispatcher = {
 *        ?a => instance_method(:do_a),
 *        ?d => instance_method(:do_d),
 *        ?e => instance_method(:do_e),
 *        ?v => instance_method(:do_v)
 *       }
 *       def interpret(string)
 *         string.each_byte {|b| Dispatcher[b].bind(self).call }
 *       end
 *     end
 *     
 *     
 *     interpreter = Interpreter.new
 *     interpreter.interpret('dave')
 *     
 *  <em>produces:</em>
 *     
 *     Hello there, Dave!
 */

static VALUE
rb_mod_method(VALUE mod, VALUE vid)
{
    return mnew(mod, Qundef, rb_to_id(vid), rb_cUnboundMethod);
}

/*
 *  call-seq:
 *     define_method(symbol, method)     => new_method
 *     define_method(symbol) { block }   => proc
 *  
 *  Defines an instance method in the receiver. The _method_
 *  parameter can be a +Proc+ or +Method+ object.
 *  If a block is specified, it is used as the method body. This block
 *  is evaluated using <code>instance_eval</code>, a point that is
 *  tricky to demonstrate because <code>define_method</code> is private.
 *  (This is why we resort to the +send+ hack in this example.)
 *     
 *     class A
 *       def fred
 *         puts "In Fred"
 *       end
 *       def create_method(name, &block)
 *         self.class.send(:define_method, name, &block)
 *       end
 *       define_method(:wilma) { puts "Charge it!" }
 *     end
 *     class B < A
 *       define_method(:barney, instance_method(:fred))
 *     end
 *     a = B.new
 *     a.barney
 *     a.wilma
 *     a.create_method(:betty) { p self }
 *     a.betty
 *     
 *  <em>produces:</em>
 *     
 *     In Fred
 *     Charge it!
 *     #<B:0x401b39e8>
 */

static VALUE
rb_mod_define_method(int argc, VALUE *argv, VALUE mod)
{
    ID id;
    VALUE body;
    NODE *node;
    int noex = NOEX_PUBLIC;

    if (argc == 1) {
	id = rb_to_id(argv[0]);
	body = rb_block_lambda();
    }
    else if (argc == 2) {
	id = rb_to_id(argv[0]);
	body = argv[1];
	if (!rb_obj_is_method(body) && !yarv_obj_is_proc(body)) {
	    rb_raise(rb_eTypeError,
		     "wrong argument type %s (expected Proc/Method)",
		     rb_obj_classname(body));
	}
    }
    else {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 1)", argc);
    }

    if (RDATA(body)->dmark == (RUBY_DATA_FUNC) bm_mark) {
	struct METHOD *method = (struct METHOD *)DATA_PTR(body);
	VALUE rklass = method->rklass;
	if (rklass != mod) {
	    if (FL_TEST(rklass, FL_SINGLETON)) {
		rb_raise(rb_eTypeError,
			 "can't bind singleton method to a different class");
	    }
	    if (!RTEST(rb_class_inherited_p(mod, rklass))) {
		rb_raise(rb_eTypeError,
			 "bind argument must be a subclass of %s",
			 rb_class2name(rklass));
	    }
	}
	node = method->body;
    }
    else if (yarv_obj_is_proc(body)) {
	yarv_proc_t *proc;
	body = yarv_proc_dup(body);
	GetProcPtr(body, proc);
	if (BUILTIN_TYPE(proc->block.iseq) != T_NODE) {
	    proc->block.iseq->defined_method_id = id;
	    proc->block.iseq->klass = mod;
	    proc->is_lambda = Qtrue;
	}
	node = NEW_BMETHOD(body);
    }
    else {
	/* type error */
	rb_raise(rb_eTypeError, "wrong argument type (expected Proc/Method)");
    }

    /* TODO: visibility */

    rb_add_method(mod, id, node, noex);
    return body;
}


/*
 * MISSING: documentation
 */

static VALUE
method_clone(VALUE self)
{
    VALUE clone;
    struct METHOD *orig, *data;

    Data_Get_Struct(self, struct METHOD, orig);
    clone =
	Data_Make_Struct(CLASS_OF(self), struct METHOD, bm_mark, free, data);
    CLONESETUP(clone, self);
    *data = *orig;

    return clone;
}

/*
 *  call-seq:
 *     meth.call(args, ...)    => obj
 *     meth[args, ...]         => obj
 *  
 *  Invokes the <i>meth</i> with the specified arguments, returning the
 *  method's return value.
 *     
 *     m = 12.method("+")
 *     m.call(3)    #=> 15
 *     m.call(20)   #=> 32
 */

VALUE
rb_method_call(int argc, VALUE *argv, VALUE method)
{
    VALUE result = Qnil;	/* OK */
    struct METHOD *data;
    int state;
    volatile int safe = -1;

    Data_Get_Struct(method, struct METHOD, data);
    if (data->recv == Qundef) {
	rb_raise(rb_eTypeError, "can't call unbound method; bind first");
    }
    PUSH_TAG(PROT_NONE);
    if (OBJ_TAINTED(method)) {
	safe = rb_safe_level();
	if (rb_safe_level() < 4) {
	    rb_set_safe_level_force(4);
	}
    }
    if ((state = EXEC_TAG()) == 0) {
	PASS_PASSED_BLOCK();
	result = th_call0(GET_THREAD(),
			  data->klass, data->recv, data->id, data->oid,
			  argc, argv, data->body, 0);
    }
    POP_TAG();
    if (safe >= 0)
	rb_set_safe_level_force(safe);
    if (state)
	JUMP_TAG(state);
    return result;
}

/**********************************************************************
 *
 * Document-class: UnboundMethod
 *
 *  Ruby supports two forms of objectified methods. Class
 *  <code>Method</code> is used to represent methods that are associated
 *  with a particular object: these method objects are bound to that
 *  object. Bound method objects for an object can be created using
 *  <code>Object#method</code>.
 *     
 *  Ruby also supports unbound methods; methods objects that are not
 *  associated with a particular object. These can be created either by
 *  calling <code>Module#instance_method</code> or by calling
 *  <code>unbind</code> on a bound method object. The result of both of
 *  these is an <code>UnboundMethod</code> object.
 *     
 *  Unbound methods can only be called after they are bound to an
 *  object. That object must be be a kind_of? the method's original
 *  class.
 *     
 *     class Square
 *       def area
 *         @side * @side
 *       end
 *       def initialize(side)
 *         @side = side
 *       end
 *     end
 *     
 *     area_un = Square.instance_method(:area)
 *     
 *     s = Square.new(12)
 *     area = area_un.bind(s)
 *     area.call   #=> 144
 *     
 *  Unbound methods are a reference to the method at the time it was
 *  objectified: subsequent changes to the underlying class will not
 *  affect the unbound method.
 *     
 *     class Test
 *       def test
 *         :original
 *       end
 *     end
 *     um = Test.instance_method(:test)
 *     class Test
 *       def test
 *         :modified
 *       end
 *     end
 *     t = Test.new
 *     t.test            #=> :modified
 *     um.bind(t).call   #=> :original
 *     
 */

/*
 *  call-seq:
 *     umeth.bind(obj) -> method
 *  
 *  Bind <i>umeth</i> to <i>obj</i>. If <code>Klass</code> was the class
 *  from which <i>umeth</i> was obtained,
 *  <code>obj.kind_of?(Klass)</code> must be true.
 *     
 *     class A
 *       def test
 *         puts "In test, class = #{self.class}"
 *       end
 *     end
 *     class B < A
 *     end
 *     class C < B
 *     end
 *     
 *     
 *     um = B.instance_method(:test)
 *     bm = um.bind(C.new)
 *     bm.call
 *     bm = um.bind(B.new)
 *     bm.call
 *     bm = um.bind(A.new)
 *     bm.call
 *     
 *  <em>produces:</em>
 *     
 *     In test, class = C
 *     In test, class = B
 *     prog.rb:16:in `bind': bind argument must be an instance of B (TypeError)
 *     	from prog.rb:16
 */

static VALUE
umethod_bind(VALUE method, VALUE recv)
{
    struct METHOD *data, *bound;

    Data_Get_Struct(method, struct METHOD, data);
    if (data->rklass != CLASS_OF(recv)) {
	if (FL_TEST(data->rklass, FL_SINGLETON)) {
	    rb_raise(rb_eTypeError,
		     "singleton method called for a different object");
	}
	if (!rb_obj_is_kind_of(recv, data->rklass)) {
	    rb_raise(rb_eTypeError, "bind argument must be an instance of %s",
		     rb_class2name(data->rklass));
	}
    }

    method = Data_Make_Struct(rb_cMethod, struct METHOD, bm_mark, free, bound);
    *bound = *data;
    bound->recv = recv;
    bound->rklass = CLASS_OF(recv);

    return method;
}

int
rb_node_arity(NODE* body)
{
    int n;

    switch (nd_type(body)) {
    case NODE_CFUNC:
	if (body->nd_argc < 0)
	    return -1;
	return body->nd_argc;
    case NODE_ZSUPER:
	return -1;
    case NODE_ATTRSET:
	return 1;
    case NODE_IVAR:
	return 0;
    case NODE_BMETHOD:
	return rb_proc_arity(body->nd_cval);
    case NODE_SCOPE:
	body = body->nd_next;	/* skip NODE_SCOPE */
	if (nd_type(body) == NODE_BLOCK)
	    body = body->nd_head;
	if (!body)
	    return 0;
	n = body->nd_frml ? RARRAY_LEN(body->nd_frml) : 0;
	if (body->nd_opt || body->nd_rest)
	    n = -n - 1;
	return n;
    case YARV_METHOD_NODE:{
	    yarv_iseq_t *iseq;
	    GetISeqPtr((VALUE)body->nd_body, iseq);
	    if (iseq->arg_rest == 0 && iseq->arg_opts == 0) {
		return iseq->argc;
	    }
	    else {
		return -iseq->argc - 1;
	    }
	}
    default:
	rb_raise(rb_eArgError, "invalid node 0x%x", nd_type(body));
    }
}

/*
 *  call-seq:
 *     meth.arity    => fixnum
 *  
 *  Returns an indication of the number of arguments accepted by a
 *  method. Returns a nonnegative integer for methods that take a fixed
 *  number of arguments. For Ruby methods that take a variable number of
 *  arguments, returns -n-1, where n is the number of required
 *  arguments. For methods written in C, returns -1 if the call takes a
 *  variable number of arguments.
 *     
 *     class C
 *       def one;    end
 *       def two(a); end
 *       def three(*a);  end
 *       def four(a, b); end
 *       def five(a, b, *c);    end
 *       def six(a, b, *c, &d); end
 *     end
 *     c = C.new
 *     c.method(:one).arity     #=> 0
 *     c.method(:two).arity     #=> 1
 *     c.method(:three).arity   #=> -1
 *     c.method(:four).arity    #=> 2
 *     c.method(:five).arity    #=> -3
 *     c.method(:six).arity     #=> -3
 *     
 *     "cat".method(:size).arity      #=> 0
 *     "cat".method(:replace).arity   #=> 1
 *     "cat".method(:squeeze).arity   #=> -1
 *     "cat".method(:count).arity     #=> -1
 */

static VALUE
method_arity_m(VALUE method)
{
    int n = method_arity(method);
    return INT2FIX(n);
}

static int
method_arity(VALUE method)
{
    struct METHOD *data;

    Data_Get_Struct(method, struct METHOD, data);
    return rb_node_arity(data->body);
}

int
rb_mod_method_arity(VALUE mod, ID id)
{
    NODE *node = rb_method_node(mod, id);
    return rb_node_arity(node);
}

int
rb_obj_method_arity(VALUE obj, ID id)
{
    return rb_mod_method_arity(CLASS_OF(obj), id);
}

/*
 *  call-seq:
 *   meth.to_s      =>  string
 *   meth.inspect   =>  string
 *
 *  Show the name of the underlying method.
 *
 *    "cat".method(:count).inspect   #=> "#<Method: String#count>"
 */

static VALUE
method_inspect(VALUE method)
{
    struct METHOD *data;
    VALUE str;
    const char *s;
    char *sharp = "#";

    Data_Get_Struct(method, struct METHOD, data);
    str = rb_str_buf_new2("#<");
    s = rb_obj_classname(method);
    rb_str_buf_cat2(str, s);
    rb_str_buf_cat2(str, ": ");

    if (FL_TEST(data->klass, FL_SINGLETON)) {
	VALUE v = rb_iv_get(data->klass, "__attached__");

	if (data->recv == Qundef) {
	    rb_str_buf_append(str, rb_inspect(data->klass));
	}
	else if (data->recv == v) {
	    rb_str_buf_append(str, rb_inspect(v));
	    sharp = ".";
	}
	else {
	    rb_str_buf_append(str, rb_inspect(data->recv));
	    rb_str_buf_cat2(str, "(");
	    rb_str_buf_append(str, rb_inspect(v));
	    rb_str_buf_cat2(str, ")");
	    sharp = ".";
	}
    }
    else {
	rb_str_buf_cat2(str, rb_class2name(data->rklass));
	if (data->rklass != data->klass) {
	    rb_str_buf_cat2(str, "(");
	    rb_str_buf_cat2(str, rb_class2name(data->klass));
	    rb_str_buf_cat2(str, ")");
	}
    }
    rb_str_buf_cat2(str, sharp);
    rb_str_buf_cat2(str, rb_id2name(data->oid));
    rb_str_buf_cat2(str, ">");

    return str;
}

static VALUE
mproc(VALUE method)
{
    return rb_funcall(Qnil, rb_intern("proc"), 0);
}

static VALUE
bmcall(VALUE args, VALUE method)
{
    volatile VALUE a;
    if (CLASS_OF(args) != rb_cArray) {
	args = rb_ary_new3(1, args);
    }

    a = args;
    return rb_method_call(RARRAY_LEN(a), RARRAY_PTR(a), method);
}

VALUE
rb_proc_new(
    VALUE (*func)(ANYARGS), /* VALUE yieldarg[, VALUE procarg] */
    VALUE val)
{
    yarv_proc_t *proc;
    VALUE procval = rb_iterate((VALUE(*)(VALUE))mproc, 0, func, val);
    GetProcPtr(procval, proc);
    ((NODE*)proc->block.iseq)->u3.state = 1;
    return procval;
}

/*
 *  call-seq:
 *     meth.to_proc    => prc
 *  
 *  Returns a <code>Proc</code> object corresponding to this method.
 */

static VALUE
method_proc(VALUE method)
{
    VALUE proc;
    /*
     * class Method
     *   def to_proc
     *     proc{|*args|
     *       self.call(*args)
     *     }
     *   end
     * end
     */
    proc = rb_iterate((VALUE (*)(VALUE))mproc, 0, bmcall, method);
    return proc;
}

static VALUE
rb_obj_is_method(VALUE m)
{
    if (TYPE(m) == T_DATA && RDATA(m)->dmark == (RUBY_DATA_FUNC) bm_mark) {
	return Qtrue;
    }
    return Qfalse;
}

/*
 * call_seq:
 *   local_jump_error.exit_value  => obj
 *
 * Returns the exit value associated with this +LocalJumpError+.
 */
static VALUE
localjump_xvalue(VALUE exc)
{
    return rb_iv_get(exc, "@exit_value");
}

/*
 * call-seq:
 *    local_jump_error.reason   => symbol
 *
 * The reason this block was terminated:
 * :break, :redo, :retry, :next, :return, or :noreason.
 */

static VALUE
localjump_reason(VALUE exc)
{
    return rb_iv_get(exc, "@reason");
}


/*
 *  <code>Proc</code> objects are blocks of code that have been bound to
 *  a set of local variables. Once bound, the code may be called in
 *  different contexts and still access those variables.
 *     
 *     def gen_times(factor)
 *       return Proc.new {|n| n*factor }
 *     end
 *     
 *     times3 = gen_times(3)
 *     times5 = gen_times(5)
 *     
 *     times3.call(12)               #=> 36
 *     times5.call(5)                #=> 25
 *     times3.call(times5.call(4))   #=> 60
 *     
 */

void
Init_Proc(void)
{
    /* Env */
    rb_cVM = rb_define_class("VM", rb_cObject); /* TODO: should be moved to suitable place */
    rb_cEnv = rb_define_class_under(rb_cVM, "Env", rb_cObject);
    rb_undef_alloc_func(rb_cEnv);

    /* Proc */
    rb_cProc = rb_define_class("Proc", rb_cObject);
    rb_undef_alloc_func(rb_cProc);
    rb_define_singleton_method(rb_cProc, "new", rb_proc_s_new, 0);
    rb_define_method(rb_cProc, "call", proc_call, -1);
    rb_define_method(rb_cProc, "[]", proc_call, -1);
    rb_define_method(rb_cProc, "yield", proc_yield, -1);
    rb_define_method(rb_cProc, "to_proc", proc_to_proc, 0);
    rb_define_method(rb_cProc, "arity", proc_arity, 0);
    rb_define_method(rb_cProc, "clone", proc_clone, 0);
    rb_define_method(rb_cProc, "dup", proc_dup, 0);
    rb_define_method(rb_cProc, "==", proc_eq, 1);
    rb_define_method(rb_cProc, "eql?", proc_eq, 1);
    rb_define_method(rb_cProc, "hash", proc_hash, 0);
    rb_define_method(rb_cProc, "to_s", proc_to_s, 0);

    /* Exceptions */
    rb_eLocalJumpError = rb_define_class("LocalJumpError", rb_eStandardError);
    rb_define_method(rb_eLocalJumpError, "exit_value", localjump_xvalue, 0);
    rb_define_method(rb_eLocalJumpError, "reason", localjump_reason, 0);
    exception_error = rb_exc_new2(rb_eFatal, "exception reentered");
    rb_register_mark_object(exception_error);

    rb_eSysStackError = rb_define_class("SystemStackError", rb_eException);
    sysstack_error = rb_exc_new2(rb_eSysStackError, "stack level too deep");
    OBJ_TAINT(sysstack_error);
    rb_register_mark_object(sysstack_error);

    /* utility functions */
    rb_define_global_function("proc", rb_block_proc, 0);
    rb_define_global_function("lambda", proc_lambda, 0);

    /* Method */
    rb_cMethod = rb_define_class("Method", rb_cObject);
    rb_undef_alloc_func(rb_cMethod);
    rb_undef_method(CLASS_OF(rb_cMethod), "new");
    rb_define_method(rb_cMethod, "==", method_eq, 1);
    rb_define_method(rb_cMethod, "eql?", method_eq, 1);
    rb_define_method(rb_cMethod, "hash", method_hash, 0);
    rb_define_method(rb_cMethod, "clone", method_clone, 0);
    rb_define_method(rb_cMethod, "call", rb_method_call, -1);
    rb_define_method(rb_cMethod, "[]", rb_method_call, -1);
    rb_define_method(rb_cMethod, "arity", method_arity_m, 0);
    rb_define_method(rb_cMethod, "inspect", method_inspect, 0);
    rb_define_method(rb_cMethod, "to_s", method_inspect, 0);
    rb_define_method(rb_cMethod, "to_proc", method_proc, 0);
    rb_define_method(rb_cMethod, "receiver", method_receiver, 0);
    rb_define_method(rb_cMethod, "name", method_name, 0);
    rb_define_method(rb_cMethod, "owner", method_owner, 0);
    rb_define_method(rb_cMethod, "unbind", method_unbind, 0);
    rb_define_method(rb_mKernel, "method", rb_obj_method, 1);

    /* UnboundMethod */
    rb_cUnboundMethod = rb_define_class("UnboundMethod", rb_cObject);
    rb_undef_alloc_func(rb_cUnboundMethod);
    rb_undef_method(CLASS_OF(rb_cUnboundMethod), "new");
    rb_define_method(rb_cUnboundMethod, "==", method_eq, 1);
    rb_define_method(rb_cUnboundMethod, "eql?", method_eq, 1);
    rb_define_method(rb_cUnboundMethod, "hash", method_hash, 0);
    rb_define_method(rb_cUnboundMethod, "clone", method_clone, 0);
    rb_define_method(rb_cUnboundMethod, "arity", method_arity_m, 0);
    rb_define_method(rb_cUnboundMethod, "inspect", method_inspect, 0);
    rb_define_method(rb_cUnboundMethod, "to_s", method_inspect, 0);
    rb_define_method(rb_cUnboundMethod, "name", method_name, 0);
    rb_define_method(rb_cUnboundMethod, "owner", method_owner, 0);
    rb_define_method(rb_cUnboundMethod, "bind", umethod_bind, 1);

    /* Module#*_method */
    rb_define_method(rb_cModule, "instance_method", rb_mod_method, 1);
    rb_define_private_method(rb_cModule, "define_method",
			     rb_mod_define_method, -1);
}

/*
 *  Objects of class <code>Binding</code> encapsulate the execution
 *  context at some particular place in the code and retain this context
 *  for future use. The variables, methods, value of <code>self</code>,
 *  and possibly an iterator block that can be accessed in this context
 *  are all retained. Binding objects can be created using
 *  <code>Kernel#binding</code>, and are made available to the callback
 *  of <code>Kernel#set_trace_func</code>.
 *     
 *  These binding objects can be passed as the second argument of the
 *  <code>Kernel#eval</code> method, establishing an environment for the
 *  evaluation.
 *     
 *     class Demo
 *       def initialize(n)
 *         @secret = n
 *       end
 *       def getBinding
 *         return binding()
 *       end
 *     end
 *     
 *     k1 = Demo.new(99)
 *     b1 = k1.getBinding
 *     k2 = Demo.new(-3)
 *     b2 = k2.getBinding
 *     
 *     eval("@secret", b1)   #=> 99
 *     eval("@secret", b2)   #=> -3
 *     eval("@secret")       #=> nil
 *     
 *  Binding objects have no class-specific methods.
 *     
 */

void
Init_Binding(void)
{
    rb_cBinding = rb_define_class("Binding", rb_cObject);
    rb_undef_alloc_func(rb_cBinding);
    rb_undef_method(CLASS_OF(rb_cBinding), "new");
    rb_define_method(rb_cBinding, "clone", binding_clone, 0);
    rb_define_method(rb_cBinding, "dup", binding_dup, 0);
    rb_define_method(rb_cBinding, "eval", bind_eval, -1);
    rb_define_global_function("binding", rb_f_binding, 0);
}

