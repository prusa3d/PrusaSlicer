info = {
    id = "hello",
    type = "project.plugin",
    title = "Hello world",
    menu = "Tutorial/Hello world",
    params = {
        {name = "text", label = "Text", type = "string", default = "Hello world"}
    }
}

function execute(opts)
    api.project:add_object{
        mesh=api.emboss_text{
             font=api.get_default_font(),
             text=opts.text,
             depth=1
         },
        object_params={fill_density="0%"}
    }
end
