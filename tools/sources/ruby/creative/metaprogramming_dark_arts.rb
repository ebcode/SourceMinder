# Metaprogramming dark arts: classes and methods conjured at runtime.
# Almost none of these method names exist in the static source text.

# Build a whole class object with no `class` keyword, then name it.
Widget = Class.new do
  attr_accessor :label

  def initialize(label)
    @label = label
  end

  def render = "[#{@label}]"
end

# Stamp constants into the namespace dynamically (Object.const_set defines
# top-level constants; a bare const_set has no Module receiver here).
%i[Red Green Blue].each_with_index do |name, code|
  Object.const_set(name, Class.new(Widget) do
    define_method(:color_code) { code }
  end)
end

# A configurable "model" whose attributes are declared by data.
class Model
  SCHEMA = { id: :int, name: :string, active: :bool }

  SCHEMA.each_key do |attribute|
    # Each generated reader/writer closes over `attribute`.
    define_method(attribute) { @attrs[attribute] }
    define_method("#{attribute}=") { |val| @attrs[attribute] = val }
    define_method("#{attribute}?") { !@attrs[attribute].nil? }
  end

  def initialize(**attrs)
    @attrs = attrs
  end
end

# Ghost methods: respond to calls that were never defined.
class ApiProxy
  def initialize(base) = @base = base

  def method_missing(name, *args, **opts, &block)
    verb, resource = name.to_s.split("_", 2)
    return super unless %w[get post delete].include?(verb)

    dispatch(verb, resource, args, opts)
  end

  def respond_to_missing?(name, include_private = false)
    name.to_s.start_with?("get_", "post_", "delete_") || super
  end

  private

  def dispatch(verb, resource, args, opts)
    "#{verb.upcase} #{@base}/#{resource} #{args.inspect}"
  end
end

# instance_eval / class_eval DSL: redefine self inside a block.
class Configuration
  def self.build(&block)
    config = new
    config.instance_eval(&block)   # `self` becomes the instance
    config
  end

  def set(key, value)
    (@settings ||= {})[key] = value
  end
end

# send / public_send reach methods chosen at runtime by string.
def invoke(object, action, *rest)
  object.public_send(action, *rest)
end

# Reopen a core class and inject behavior (monkey patching).
class String
  def shout
    "#{upcase}!!!"
  end
end

# --- Exercise: instantiate and call the runtime-built definitions. ---
widget = Widget.new("save")
swatch = Red.new("crimson")
model  = Model.new(id: 1, name: "Ada", active: true)
model.name = "Grace"
proxy  = ApiProxy.new("https://api.test")
config = Configuration.build { set(:timeout, 30) }
greeting = invoke("hello", :shout)
puts [widget.render, swatch.color_code, model.name, model.active?,
      proxy.get_users, config, greeting].inspect
