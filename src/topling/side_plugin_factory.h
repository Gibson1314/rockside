//
// Created by leipeng on 2020-06-29.
//
// side_plugin_repo.h    is mostly for plugin users
// side_plugin_factory.h is mostly for plugin developers
//
#pragma once

#include <memory>

#include "side_plugin_repo.h"
#include "web/json_civetweb.h"
#include "json.h"
#include "rocksdb/enum_reflection.h"
#include "rocksdb/preproc.h"
#include "rocksdb/status.h"

namespace ROCKSDB_NAMESPACE {

#define THROW_STATUS(Type, msg) throw rocksdb::Status::Type(std::string(__FILE__) + (":" ROCKSDB_PP_STR(__LINE__) ": ") + ROCKSDB_FUNC, msg)
#define THROW_InvalidArgument(msg) THROW_STATUS(InvalidArgument, msg)
#define THROW_Corruption(msg) THROW_STATUS(Corruption, msg)
#define THROW_NotFound(msg) THROW_STATUS(NotFound, msg)
#define THROW_NotSupported(msg) THROW_STATUS(NotSupported, msg)

using nlohmann::json;

template<class P> struct RemovePtr_tpl; // NOLINT
template<class T> struct RemovePtr_tpl<T*> { typedef T type; };
template<class T> struct RemovePtr_tpl<std::shared_ptr<T> > { typedef T type; };
template<> struct RemovePtr_tpl<DB_Ptr> { typedef DB type; };
template<class P> using RemovePtr = typename RemovePtr_tpl<P>::type;

template<class T> T* GetRawPtr(T* p){ return p; }
template<class T> T* GetRawPtr(const std::shared_ptr<T>& p){ return p.get(); }
inline DB* GetRawPtr(const DB_Ptr& p){ return p.db; }

struct CFPropertiesWebView {
  DB* db;
  ColumnFamilyHandle* cfh;
};
struct SidePluginRepo::Impl {
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl();
  ~Impl();

  struct ObjInfo {
    std::string name;
    json params; // { class : "class_name", params : "params..." }
  };
  template<class Ptr>
  struct ObjMap {
    std::map<const void*, ObjInfo> p2name;
    std::shared_ptr<std::map<std::string, Ptr>> name2p =
        std::make_shared<std::map<std::string, Ptr>>();
  };
  template<class T>
  using ObjRepo = ObjMap<std::shared_ptr<T> >;

  ObjRepo<AnyPlugin> any_plugin;
  ObjRepo<Cache> cache;
  ObjRepo<PersistentCache> persistent_cache;
  ObjRepo<CompactionExecutorFactory> compaction_executor_factory;
  ObjRepo<CompactionFilterFactory> compaction_filter_factory;
  ObjMap<const Comparator*> comparator;
  ObjRepo<ConcurrentTaskLimiter> compaction_thread_limiter;
  ObjMap<Env*> env;
  ObjRepo<EventListener> event_listener;
  ObjRepo<FileChecksumGenFactory> file_checksum_gen_factory;
  ObjRepo<FileSystem> file_system;
  ObjRepo<const FilterPolicy> filter_policy;
  ObjRepo<FlushBlockPolicyFactory> flush_block_policy_factory;
  ObjRepo<Logger> info_log;
  ObjRepo<MemoryAllocator> memory_allocator;
  ObjRepo<MemTableRepFactory> memtable_factory;
  ObjRepo<MergeOperator> merge_operator;
  ObjRepo<RateLimiter> rate_limiter;
  ObjRepo<SstFileManager> sst_file_manager;
  ObjRepo<SstPartitionerFactory> sst_partitioner_factory;
  ObjRepo<Statistics> statistics;
  ObjRepo<TableFactory> table_factory;
  ObjRepo<TablePropertiesCollectorFactory> table_properties_collector_factory;
  ObjRepo<TransactionDBMutexFactory> txn_db_mutex_factory;
  ObjRepo<const SliceTransform> slice_transform;

  ObjRepo<Options> options;
  ObjRepo<DBOptions> db_options;
  ObjRepo<ColumnFamilyOptions> cf_options;

  ObjRepo<CFPropertiesWebView> props;
  ObjMap<DB_Ptr> db;

  json db_js; // not evaluated during import
  json open_js;
  json http_js;

  JsonCivetServer http;
};
struct DB_MultiCF_Impl : public DB_MultiCF {
  DB_MultiCF_Impl();
  ~DB_MultiCF_Impl() override;
  ColumnFamilyHandle* Get(const std::string& cfname) const override;
  Status CreateColumnFamily(const std::string& cfname, const std::string& json_str, ColumnFamilyHandle**) override;
  Status DropColumnFamily(const std::string& cfname) override;
  Status DropColumnFamily(ColumnFamilyHandle*) override;
  void AddOneCF_ToMap(const std::string& cfname, ColumnFamilyHandle*, const json&);
  void InitAddCF_ToMap(const json& js_cf_desc);
  SidePluginRepo::Impl::ObjMap<ColumnFamilyHandle*> m_cfhs;
  const SidePluginRepo* m_repo;
  std::function<ColumnFamilyHandle*
    (DB*, const std::string& cfname, const ColumnFamilyOptions&, const json& extra_args)
   > m_create_cf;
};
template<class Ptr>
Ptr ObtainOPT(SidePluginRepo::Impl::ObjMap<Ptr>& field,
              const char* option_class, // "DBOptions" or "CFOptions"
              const json& option_js, const SidePluginRepo& repo);

///@note on principle, the factory itself is stateless, but its product
/// can has states, sometimes we need factory of factory, in this case,
/// just let the factory being PluginFactory:
/// class SomeClass : public PluginFactory<SomeClass*> {...};
/// class SomeClass : public PluginFactory<shared_ptr<SomeClass> > {...};
template<class Ptr>
class PluginFactory {
public:
  virtual ~PluginFactory() = default;
  // in some contexts Acquire means 'CreateNew'
  // in some contexts Acquire means 'GetExisting'
  static Ptr AcquirePlugin(const std::string& clazz, const json&,
                           const SidePluginRepo&);

  // json is string class_name or
  // object{ class: "class_name", params: {...} }
  // throw if not found
  static Ptr AcquirePlugin(const json&, const SidePluginRepo&);

  // not throw if plugin does not exist
  static Ptr NullablePlugin(const std::string& clazz,
                            const json& = json(),
                            const SidePluginRepo& = SidePluginRepo());

  static Ptr ObtainPlugin(const char* varname, const char* func_name,
                          const json&, const SidePluginRepo&);

  static Ptr GetPlugin(const char* varname, const char* func_name,
                       const json&, const SidePluginRepo&);

  static bool HasPlugin(const std::string& class_name);
  static bool SamePlugin(const std::string& clazz1, const std::string& clazz2);

  typedef Ptr (*AcqFunc)(const json&,const SidePluginRepo&);
  struct Meta {
    AcqFunc acq;
    std::string base_class; // chain to base
  };
  struct Reg {
    Reg(const Reg&) = delete;
    Reg(Reg&&) = delete;
    Reg& operator=(Reg&&) = delete;
    Reg& operator=(const Reg&) = delete;
    using NameToFuncMap = std::map<std::string, Meta>;
    Reg(Slice class_name, AcqFunc acq, Slice base_class = "") noexcept;
    ~Reg();
    typename NameToFuncMap::iterator ipos;
    struct Impl;
  };
};
template<class Object>
using PluginFactorySP = PluginFactory<std::shared_ptr<Object> >;

template<class Object>
struct PluginManipFunc {
  virtual ~PluginManipFunc() {}
  virtual void Update(Object*, const json&, const SidePluginRepo&) const = 0;
  virtual std::string ToString(const Object&, const json&, const SidePluginRepo&) const = 0;
  using InterfaceType = PluginManipFunc;
};
template<class ManipClass>
static const typename ManipClass::InterfaceType*
PluginManipSingleton(const json&, const SidePluginRepo&) {
  static const ManipClass manip;
  return &manip;
}
#define ROCKSDB_REG_PluginManip(ClassName, ManipClass) \
  PluginFactory<const typename ManipClass::InterfaceType*>::Reg \
      ROCKSDB_PP_CAT_3(g_reg_manip_,ManipClass,__LINE__) \
     (ClassName, &PluginManipSingleton<ManipClass>)

template<class Object>
using PluginManip = PluginFactory<const PluginManipFunc<Object>*>;
template<class Ptr>
void PluginUpdate(const Ptr& p, const SidePluginRepo::Impl::ObjMap<Ptr>& map,
                  const json& js, const SidePluginRepo& repo) {
  using Object = RemovePtr<Ptr>;
  auto iter = map.p2name.find(GetRawPtr(p));
  if (map.p2name.end() != iter) {
    auto manip = PluginManip<Object>::AcquirePlugin(iter->second.params, repo);
    manip->Update(GetRawPtr(p), js, repo);
  }
}
template<class Ptr>
std::string
PluginToString(const Ptr& p, const SidePluginRepo::Impl::ObjMap<Ptr>& map,
               const json& js, const SidePluginRepo& repo) {
  using Object = RemovePtr<Ptr>;
  auto iter = map.p2name.find(GetRawPtr(p));
  if (map.p2name.end() != iter) {
    auto manip = PluginManip<Object>::AcquirePlugin(iter->second.params, repo);
    return manip->ToString(*p, js, repo);
  }
  THROW_NotFound("Ptr not found");
}
std::string
PluginToString(const DB_Ptr&, const SidePluginRepo::Impl::ObjMap<DB_Ptr>& map,
               const json& js, const SidePluginRepo& repo);

// use SerDeFunc as plugin, register SerDeFunc as plugin
template<class Object>
struct SerDeFunc {
  virtual ~SerDeFunc() {}
  virtual void Serialize(FILE*, const Object&) const = 0;
  virtual void DeSerialize(FILE*, Object*) const = 0;
  using InterfaceType = SerDeFunc;
};
template<class SerDeClass>
static std::shared_ptr<typename SerDeClass::InterfaceType>
PluginSerDeSingleton(const json&, const SidePluginRepo&) {
  static auto singleton = std::make_shared<SerDeClass>();
  return singleton;
}
#define ROCKSDB_REG_PluginSerDe(ClassName, SerDeClass) \
  PluginFactory<std::shared_ptr<typename SerDeClass::InterfaceType> >::Reg \
      ROCKSDB_PP_CAT_3(g_reg_serde_,SerDeClass,__LINE__) \
     (ClassName, &PluginSerDeSingleton<SerDeClass>)

template<class Object>
using SerDeFactory = PluginFactory<std::shared_ptr<SerDeFunc<Object> > >;

// Suffix 'Req' means 'required'
template<class Object>
void SerDe_SerializeReq(FILE* fp, const std::string& clazz, const Object* obj,
                        const json& js = json{},
                        const SidePluginRepo& repo = *(SidePluginRepo*)nullptr) {
  assert(nullptr != obj);
  auto serde = SerDeFactory<Object>::AcquirePlugin(clazz, js, repo);
  serde->Serialize(fp, *obj);
}
template<class Object>
void SerDe_SerializeReq(FILE* fp, const std::string& clazz,
                        const std::shared_ptr<Object>& obj,
                        const json& js = json{},
                        const SidePluginRepo& repo = *(SidePluginRepo*)nullptr) {
  return SerDe_SerializeReq(fp, clazz, obj.get(), js, repo);
}

// Suffix 'Opt' means 'optional'
// if clazz has no SerDeFunc, obj can     be null
// if clazz has    SerDeFunc, obj can not be null
template<class Object>
void SerDe_SerializeOpt(FILE* fp, const std::string& clazz, const Object* obj,
                        const json& js = json{},
                        const SidePluginRepo& repo = *(SidePluginRepo*)nullptr) {
  auto serde = SerDeFactory<Object>::NullablePlugin(clazz, js, repo);
  if (serde) {
    assert(nullptr != obj);
    serde->Serialize(fp, *obj);
  }
}
template<class Object>
void SerDe_SerializeOpt(FILE* fp, const std::string& clazz,
                        const std::shared_ptr<Object>& obj,
                        const json& js = json{},
                        const SidePluginRepo& repo = *(SidePluginRepo*)nullptr) {
  return SerDe_SerializeOpt(fp, clazz, obj.get(), js, repo);
}

template<class Object>
void SerDe_DeSerialize(FILE* fp, const std::string& clazz, Object* obj,
                       const json& js = json{},
                       const SidePluginRepo& repo = *(SidePluginRepo*)nullptr) {
  auto serde = SerDeFactory<Object>::NullablePlugin(clazz, js, repo);
  if (serde) {
    assert(nullptr != obj);
    serde->DeSerialize(fp, obj);
  }
}
template<class Object>
void SerDe_DeSerialize(FILE* fp, const std::string& clazz,
                       const std::shared_ptr<Object>& obj,
                       const json& js = json{},
                       const SidePluginRepo& repo = *(SidePluginRepo*)nullptr) {
  SerDe_DeSerialize(fp, clazz, obj.get(), js, repo);
}
template<class Ptr>
void SerDe_DeSerialize(FILE* fp, const Ptr& p,
                       const json& js = json{},
                       const SidePluginRepo& repo = *(SidePluginRepo*)nullptr) {
  if (p)
    SerDe_DeSerialize(fp, p->Name(), p, js, repo);
}

template<class Object, class Extra>
struct ExtraBinderFunc {
  virtual ~ExtraBinderFunc() {}
  virtual void Bind(Object*, Extra*) const = 0;
};
template<class Object, class Extra>
using ExtraBinder = PluginFactory<const ExtraBinderFunc<Object, Extra>*>;

struct AnyPluginManip : public PluginManipFunc<AnyPlugin> {
  void Update(AnyPlugin*, const json&, const SidePluginRepo&) const final;
  std::string ToString(const AnyPlugin&, const json& dump_options,
                       const SidePluginRepo&) const final;
};
#define ROCKSDB_REG_AnyPluginManip(ClassName) \
  PluginFactory<const PluginManipFunc<AnyPlugin>*>::Reg \
      ROCKSDB_PP_CAT_2(g_reg_manip_any_plugin_,__LINE__) \
     (ClassName, &PluginManipSingleton<AnyPluginManip>)

/// Concrete class defined non-virtual Update() & ToString()
/// EasyProxyManip is a proxy which forward Update() & ToString() to Concrete
template<class Concrete, class Interface>
struct EasyProxyManip : public PluginManipFunc<Interface> {
  void Update(Interface* x, const json& j, const SidePluginRepo& r)
  const final {
    assert(dynamic_cast<Concrete*>(x) != nullptr);
    return static_cast<Concrete*>(x)->Update(j, r);
  }
  std::string ToString(const Interface& x, const json& dump_options,
                       const SidePluginRepo& r) const final {
    assert(dynamic_cast<const Concrete*>(&x) != nullptr);
    return static_cast<const Concrete&>(x).ToString(dump_options, r);
  }
};
#define ROCKSDB_REG_EasyProxyManip_3(ClassName, ClassType, Interface) \
  PluginFactory<const PluginManipFunc<Interface>*>::Reg \
      ROCKSDB_PP_CAT_3(g_reg_manip_,ClassType,__LINE__) \
     (ClassName, &PluginManipSingleton<EasyProxyManip<ClassType, Interface> >)
#define ROCKSDB_REG_EasyProxyManip_2(ClassType, Interface) \
        ROCKSDB_REG_EasyProxyManip_3(#ClassType, ClassType, Interface)
// call ROCKSDB_REG_EasyProxyManip_${ArgNum}, ArgNum must be 2 or 3
#define ROCKSDB_REG_EasyProxyManip(...) ROCKSDB_PP_CAT2 \
       (ROCKSDB_REG_EasyProxyManip_,ROCKSDB_PP_ARG_N(__VA_ARGS__))(__VA_ARGS__)

template<class Ptr>
struct PluginFactory<Ptr>::Reg::Impl {
  NameToFuncMap func_map;
  std::map<std::string, Ptr> inst_map;
  static Impl& s_singleton() { static Impl imp; return imp; }
};

template<class Ptr>
PluginFactory<Ptr>::Reg::Reg(Slice class_name, AcqFunc acq, Slice base_class) noexcept {
  auto& imp = Impl::s_singleton();
  Meta meta{acq, std::string(base_class.data(), base_class.size())};
  auto ib = imp.func_map.insert(std::make_pair(class_name.ToString(), std::move(meta)));
  ROCKSDB_VERIFY_F(ib.second, "duplicate class_name = %s", class_name.data());
  if (SidePluginRepo::DebugLevel() >= 1) {
    fprintf(stderr, "INFO: %s: class = %s\n", ROCKSDB_FUNC, class_name.data());
  }
  this->ipos = ib.first;
}

template<class Ptr>
PluginFactory<Ptr>::Reg::~Reg() {
  auto& imp = Impl::s_singleton();
  imp.func_map.erase(ipos);
}

template<class Ptr>
Ptr
PluginFactory<Ptr>::AcquirePlugin(const std::string& clazz, const json& js,
                                  const SidePluginRepo& repo) {
  auto& imp = Reg::Impl::s_singleton();
  auto iter = imp.func_map.find(clazz);
  if (imp.func_map.end() != iter) {
    Ptr ptr = iter->second.acq(js, repo);
    assert(!!ptr);
    return ptr;
  }
  else {
    //return Ptr(nullptr);
    THROW_NotFound("class = " + clazz + ", params = " + js.dump());
  }
}

template<class Ptr>
Ptr
PluginFactory<Ptr>::NullablePlugin(const std::string& clazz, const json& js,
                                   const SidePluginRepo& repo) {
  auto& imp = Reg::Impl::s_singleton();
  auto iter = imp.func_map.find(clazz);
  if (imp.func_map.end() != iter) {
    Ptr ptr = iter->second.acq(js, repo);
    assert(!!ptr);
    return ptr;
  }
  return Ptr(nullptr);
}

std::string PluginParseInstID(const std::string& str_val);

template<class Ptr>
Ptr
PluginFactory<Ptr>::
GetPlugin(const char* varname, const char* func_name,
          const json& js, const SidePluginRepo& repo) {
  if (js.is_string()) {
    const auto& str_val = js.get_ref<const std::string&>();
    if (str_val.empty()) {
      throw Status::InvalidArgument(
          func_name, std::string(varname) + " inst_id/class_name is empty");
    }
    Ptr p(nullptr);
    bool ret = false;
    if ('$' == str_val[0]) {
      if (str_val.size() < 3) {
        throw Status::InvalidArgument(func_name,
                   std::string(varname) + " inst_id is too short");
      }
      const auto inst_id = PluginParseInstID(str_val);
      ret = repo.Get(inst_id, &p);
    } else {
      ret = repo.Get(str_val, &p); // the whole str_val is inst_id
    }
    if (!ret) {
      throw Status::NotFound(func_name,
            std::string(varname) + "inst_id = " + str_val);
    }
    assert(!!p);
    return p;
  }
  else {
    throw Status::InvalidArgument(func_name,
      std::string(varname) + " must be a string for reference to object");
  }
}

///@param varname just for error report
///@param func_name just for error report
//
// if json is a string ${inst_id} or $inst_id, then Get the plugin named
// inst_id in repo.
//
// if json is a string does not like ${inst_id} or $inst_id, then the string
// is treated as a class name to create the plugin with empty json params.
//
// if json is an object, it should be { class: class_name, params: ... }
template<class Ptr>
Ptr
PluginFactory<Ptr>::
ObtainPlugin(const char* varname, const char* func_name,
             const json& js, const SidePluginRepo& repo) {
  if (js.is_string()) {
    const auto& str_val = js.get_ref<const std::string&>();
    if (str_val.empty()) {
      throw Status::InvalidArgument(func_name, std::string(varname) +
               " inst_id/class_name is empty");
    }
    if ('$' == str_val[0]) {
      if (str_val.size() < 3) {
        throw Status::InvalidArgument(func_name, std::string(varname) +
                 " inst_id = \"" + str_val + "\" is too short");
      }
      const auto inst_id = PluginParseInstID(str_val);
      Ptr p(nullptr);
      if (!repo.Get(inst_id, &p)) {
        throw Status::NotFound(func_name,
           std::string(varname) + " inst_id = \"" + inst_id + "\"");
      }
      assert(!!p);
      return p;
    } else {
      // string which does not like ${inst_id} or $inst_id
      // try to treat str_val as inst_id to Get it
      Ptr p(nullptr);
      if (repo.Get(str_val, &p)) {
        assert(!!p);
        return p;
      }
      // now treat str_val as class name, try to --
      // AcquirePlugin with empty json params
      const std::string& clazz_name = str_val;
      return AcquirePlugin(clazz_name, json{}, repo);
    }
  } else if (js.is_null()) {
    return Ptr(nullptr);
  } else if (js.is_object()) {
    auto iter = js.find("class");
    if (js.end() == iter) {
        throw Status::InvalidArgument(func_name, "sub obj class is required");
    }
    if (!iter.value().is_string()) {
        throw Status::InvalidArgument(func_name, "sub obj class must be string");
    }
    const auto& clazz_name = iter.value().get_ref<const std::string&>();
    const json& params = js.at("params");
    return AcquirePlugin(clazz_name, params, repo);
  }
  throw Status::InvalidArgument(func_name,
      "js must be string, null, or object, but is: " + js.dump());
}

template<class Ptr>
Ptr
PluginFactory<Ptr>::
AcquirePlugin(const json& js, const SidePluginRepo& repo) {
  if (js.is_string()) {
    const auto& str_val = js.get_ref<const std::string&>();
    if (str_val.empty()) {
      THROW_InvalidArgument("jstr class_name is empty");
    }
    // now treat js as class name, try to --
    // AcquirePlugin with empty json params
    const std::string& clazz_name = str_val;
    return AcquirePlugin(clazz_name, json{}, repo);
  } else if (js.is_null()) {
    return Ptr(nullptr);
  } else if (js.is_object()) {
    auto iter = js.find("class");
    if (js.end() == iter) {
      THROW_InvalidArgument("js[\"class\"] is required: " + js.dump());
    }
    if (!iter.value().is_string()) {
      THROW_InvalidArgument("js[\"class\"] must be string: " + js.dump());
    }
    const auto& clazz_name = iter.value().get_ref<const std::string&>();
    const json& params = js.at("params");
    return AcquirePlugin(clazz_name, params, repo);
  }
  THROW_InvalidArgument(
        "js must be string, null, or object, but is: " + js.dump());
}

template<class Ptr>
bool PluginFactory<Ptr>::HasPlugin(const std::string& class_name) {
  auto& imp = Reg::Impl::s_singleton();
  return imp.func_map.count(class_name) != 0;
}

// plugin can have alias class name, this function check whether the two
// aliases are defined as a same plugin
template<class Ptr>
bool PluginFactory<Ptr>::SamePlugin(const std::string& clazz1,
                                    const std::string& clazz2) {
  if (clazz1 == clazz2) {
    return true;
  }
  auto& imp = Reg::Impl::s_singleton();
  auto i1 = imp.func_map.find(clazz1);
  auto i2 = imp.func_map.find(clazz2);
  if (imp.func_map.end() == i1) {
    THROW_NotFound("clazz1 = " + clazz1);
  }
  if (imp.func_map.end() == i2) {
    THROW_NotFound("clazz2 = " + clazz2);
  }
  return i1->second.acq == i2->second.acq;
}

const json& jsonRefType();
const SidePluginRepo& repoRefType();

///@param Name     string of factory class_name
///@param Acquire  must return base class ptr
#define ROCKSDB_FACTORY_REG(Name, Acquire) \
  PluginFactory<decltype(Acquire(jsonRefType(),repoRefType()))>:: \
  Reg ROCKSDB_PP_CAT_3(g_reg_factory_,Acquire,__LINE__)(Name,Acquire)

template<class ConcretClass, class Interface>
std::shared_ptr<Interface>
JS_NewDefaultConsObject(const json& js, const SidePluginRepo&) {
  return std::make_shared<ConcretClass>();
}
template<class ConcretClass, class Interface>
std::shared_ptr<Interface>
JS_NewJsonRepoConsObject(const json& js, const SidePluginRepo& repo) {
  return std::make_shared<ConcretClass>(js, repo);
}
#define ROCKSDB_REG_DEFAULT_CONS_3(Name, ConcretClass, Interface) \
  PluginFactory<std::shared_ptr<Interface> >::Reg \
      ROCKSDB_PP_CAT_3(g_reg_factory_,ConcretClass,__LINE__) \
     (Name,  &JS_NewDefaultConsObject<ConcretClass,Interface>)
#define ROCKSDB_REG_JSON_REPO_CONS_3(Name, ConcretClass, Interface) \
  PluginFactory<std::shared_ptr<Interface> >::Reg \
      ROCKSDB_PP_CAT_3(g_reg_factory_,ConcretClass,__LINE__) \
     (Name, &JS_NewJsonRepoConsObject<ConcretClass,Interface>)

#define ROCKSDB_REG_DEFAULT_CONS_2(ConcretClass, Interface) \
        ROCKSDB_REG_DEFAULT_CONS_3(#ConcretClass, ConcretClass, Interface)
#define ROCKSDB_REG_JSON_REPO_CONS_2(ConcretClass, Interface) \
        ROCKSDB_REG_JSON_REPO_CONS_3(#ConcretClass, ConcretClass, Interface)

// call ROCKSDB_REG_DEFAULT_CONS_${ArgNum}, ArgNum must be 2 or 3
#define ROCKSDB_REG_DEFAULT_CONS(...) ROCKSDB_PP_CAT2 \
       (ROCKSDB_REG_DEFAULT_CONS_,ROCKSDB_PP_ARG_N(__VA_ARGS__))(__VA_ARGS__)
// call ROCKSDB_REG_JSON_REPO_CONS_${ArgNum}, ArgNum must be 2 or 3
#define ROCKSDB_REG_JSON_REPO_CONS(...) ROCKSDB_PP_CAT2 \
       (ROCKSDB_REG_JSON_REPO_CONS_,ROCKSDB_PP_ARG_N(__VA_ARGS__))(__VA_ARGS__)

//////////////////////////////////////////////////////////////////////////////

#define ROCKSDB_JSON_XXX_PROP(js, prop) \
    auto __iter = js.find(#prop); \
    if (js.end() != __iter) try { \
      prop = __iter.value().get<decltype(prop)>(); \
    } catch (const std::exception& ex) {     \
      THROW_InvalidArgument( \
        "\"" #prop "\": js = " + js.dump() + " => " + ex.what()); \
    }

// _REQ_ means 'required'
// _OPT_ means 'optional'
#define ROCKSDB_JSON_REQ_PROP(js, prop) do { \
    ROCKSDB_JSON_XXX_PROP(js, prop)          \
    else THROW_InvalidArgument(             \
      "missing required param \"" #prop "\", js = " + js.dump()); \
  } while (0)
#define ROCKSDB_JSON_OPT_PROP(js, prop) do { \
    ROCKSDB_JSON_XXX_PROP(js, prop)          \
  } while (0)
#define ROCKSDB_JSON_REQ_SIZE(js, prop) prop = ParseSizeXiB(js, #prop)
#define ROCKSDB_JSON_OPT_SIZE(js, prop) do try { \
      prop = ParseSizeXiB(js, #prop); \
    } catch (const std::exception&) {} while (0)
#define ROCKSDB_JSON_OPT_ENUM(js, prop) do { \
    auto __iter = js.find(#prop); \
    if (js.end() != __iter) {                \
      if (!__iter.value().is_string())       \
        THROW_InvalidArgument("enum \"" #prop "\" must be json string"); \
      const auto& __val = __iter.value().get_ref<const std::string&>(); \
      if (!enum_value(__val, &prop)) \
        THROW_InvalidArgument("bad " #prop " = " + __val); \
  }} while (0)
#define ROCKSDB_JSON_OPT_NEST(js, prop) \
  do try { \
    auto __iter = js.find(#prop); \
    if (js.end() != __iter) \
      prop = decltype(NestForBase(prop))(__iter.value()); \
  } catch (const std::exception& ex) { \
    THROW_InvalidArgument(std::string(#prop ": ") + ex.what()); \
  } while (0)

#define ROCKSDB_JSON_OPT_FACT_INNER(js, prop) \
    prop = PluginFactory<decltype(prop)>:: \
        ObtainPlugin(#prop, ROCKSDB_FUNC, js, repo)
#define ROCKSDB_JSON_OPT_FACT(js, prop) do { \
    auto __iter = js.find(#prop); \
    if (js.end() != __iter) { \
      ROCKSDB_JSON_OPT_FACT_INNER(__iter.value(), prop); \
  }} while (0)

#define ROCKSDB_JSON_SET_SIZE(js, prop) js[#prop] = SizeToString(prop)
#define ROCKSDB_JSON_SET_PROP(js, prop) js[#prop] = prop
#define ROCKSDB_JSON_SET_ENUM(js, prop) js[#prop] = enum_stdstr(prop)
#define ROCKSDB_JSON_SET_NEST(js, prop) \
  static_cast<const decltype(NestForBase(prop))&>(prop).SaveToJson(js[#prop])

/// for which prop and repo_field with different name
#define ROCKSDB_JSON_SET_FACX(js, prop, repo_field) \
        ROCKSDB_JSON_SET_FACT_INNER(js[#prop], prop, repo_field)

/// this Option and repo has same name prop
#define ROCKSDB_JSON_SET_FACT(js, prop) \
        ROCKSDB_JSON_SET_FACT_INNER(js[#prop], prop, prop)

#define ROCKSDB_JSON_SET_FACT_INNER(inner, prop, repo_field) \
  JsonRepoSet(inner, prop, repo.m_impl->repo_field, #repo_field, html)


bool SameVarName(const std::string&, const std::string&);
bool JsonSmartBool(const json& js, const char* subname);
int  JsonSmartInt(const json& js, const char* subname, int Default);
int64_t JsonSmartInt64(const json& js, const char* subname, int64_t Default);
std::string SizeToString(unsigned long long val);

std::string
JsonRepoGetHtml_ahref(const char* mapname, const std::string& varname);
void
JsonRepoSetHtml_ahref(json&, const char* mapname, const std::string& varname);

template<class Ptr, class Map>
void JsonRepoSet(json& js, const Ptr& prop, const Map& map,
                 const char* mapname, bool html) {
  auto& p2name = map.p2name;
  auto iter = p2name.find(GetRawPtr(prop));
  if (p2name.end() != iter) {
    if (iter->second.name.empty())
      js = iter->second.params;
    else if (html)
      JsonRepoSetHtml_ahref(js, mapname, iter->second.name);
    else
      js = "${" + iter->second.name + "}";
  }
  else {
      js = "$(BuiltinDefault)";
  }
}

} // ROCKSDB_NAMESPACE
