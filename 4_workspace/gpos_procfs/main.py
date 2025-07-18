try: 
    while True:
        # Abro archivo con permisos de escritura, tomo de consola y escribo
        with open("/proc/utn-fra-td3/test", "w") as f:
            f.write(input("Ingrese un texto a escribir: "))
            f.close()
        # Abro archivo con permisos de lectura, leo y muestro por consola
        with open("/proc/utn-fra-td3/test", "r") as f:
            print("Se escribio en el proc file:", f.read())
            f.close()

except:
    f.close()
    print("Fin de la aplicacion")